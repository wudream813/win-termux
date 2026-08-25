// termux.cpp - Windows Terminal Multiplexer v1.1.1
// ---------------------------------------------------------------------------
// v8.3 changes:
//  19. ConPTY line-width autodetect: legacy full-screen apps (edit.com...) can
//      resize the console window without ConPTY emitting "CSI 8t". ConPTY then
//      streams rows padded to ITS width, which mismatches our model width and
//      produces the staircase misalignment. detect_conpty_width() spots
//      "content packed onto the tail of a padded row" and resizes the model to
//      the real ConPTY width automatically.
//      The detector is deliberately conservative so it never misfires on
//      legitimate content:
//        - the padding run must be >= 50 spaces (ConPTY pads every row to its
//          full width, so the run is huge; edit.com's dialogs only use ~24);
//        - it must be the FIRST long run after content (not the last - the
//          packed-in row may itself end in spaces);
//        - the packed-in content must be >= 2 columns (a single trailing glyph
//          is normally the app's cursor block drawn after "\x1b[K").
//      The model re-adapts each frame ("\x1b[H" resets the cooldown) and when
//      the host window is resized.
// ---------------------------------------------------------------------------
// Compile (MSVC): cl /O2 termux.cpp /link user32.lib
// Compile (MinGW): x86_64-w64-mingw32-gcc -O2 -o termux.exe termux.cpp -luser32
//
// ---------------------------------------------------------------------------
// v8.2 changes:
//  17. Wide-char (CJK) column accounting: the screen model stored every
//      character in exactly ONE column, but CJK glyphs (文件, edit.com's
//      Chinese menus, etc.) occupy TWO columns in any real terminal. Every
//      line containing such characters therefore shifted right relative to the
//      real display - the farther right, the worse - which is exactly the
//      "misaligned edit.com" symptom (it looks fine in Windows Terminal, whose
//      renderer tracks character width; our model didn't). Now a wide char is
//      stored in two cells (glyph + 0-fill marker) exactly like the console
//      buffer, and the renderer skips the 0-fill cells, so model columns ==
//      terminal columns.
//  18. TERMUX_DUMP=1 environment variable: dumps the raw ConPTY byte stream
//      per pane to termux_dump.log (diagnostic aid for display bugs).
// ---------------------------------------------------------------------------
// v8 changes vs v7:
//  14. Pane auto-close: the previous version relied on the ConPTY output pipe
//      hitting EOF when the child exited, but ReadFile blocks forever when the
//      pipe's write end is still open (ConPTY host keeps it open) - so dead
//      panes never closed their tabs. v8 polls the child PROCESS HANDLE every
//      ~30ms, gives the reader a short drain grace, then marks the pane dead
//      and reaps it. close_pane() is now critical-section safe (detach under
//      lock, free under lock) so a running reader can never touch freed memory.
//  15. Full-screen / legacy apps (edit, mode con:...) resize the console
//      buffer; ConPTY reports this as "CSI 8 ; rows ; cols t". v8 follows that
//      resize in the pane's virtual screen instead of ignoring it (fixed the
//      misaligned layout). Also handles CSI ?1048 h/l (save/restore cursor).
//  16. render_screen clears leftover rows when the pane's virtual screen is
//      smaller than the host area, so stale content doesn't linger below the
//      pane and look like misalignment.
// ---------------------------------------------------------------------------
// v7 hardening changes vs v6 (this file keeps the same architecture / UI):
//
//  [crash fixes]
//   1. VT parser: ESC followed by a C0 control byte (e.g. "ESC CR") used to
//      recurse forever -> stack overflow. The C0 is now executed in ground
//      state instead.
//   2. screen_scroll_up/down: CSI S/T/L/M with a huge parameter wrote before
//      the start of the buffer (heap corruption / crash). Scroll counts are
//      now clamped to the scroll-region height and the region is validated.
//   3. parse_params: very long digit runs overflowed `int` (UB). Values are
//      now clamped.
//   4. UTF-8 decoder state was a single static shared by ALL panes, so
//      interleaved reads from two panes corrupted each other's text. State is
//      now per-pane; stray/overlong/incomplete sequences are dropped safely.
//   5. render_screen output buffer was sized for ~20 bytes/cell but a worst
//      case cell costs ~18 bytes of escapes + up to 3 bytes UTF-8, and a
//      truncated snprintf could push the offset past the end. Buffer is now
//      sized with a comfortable margin and every section is bounded.
//   6. Allocation failures in screen_init / screen_resize / render_screen are
//      checked instead of dereferencing NULL.
//
//  [correctness]
//   7. OSC window-title now targets the pane that emitted it (previously it
//      always retitled g_mux.active_pane, so a background pane changed the
//      foreground pane's tab label).
//   8. Ctrl+Enter sends LF, matching terminal conventions.
//
//  [resource hygiene]
//   9. When a child process exits, its pane is reaped automatically: the
//      ConPTY, pipes, process/thread handles and buffers are released instead
//      of leaking an (invisible) dead slot that never got cleaned up.
//  10. close_pane tolerates already-dead processes/threads and waits longer
//      before giving up; _beginthreadex failure is cleaned up properly.
//
//  [console safety]
//  11. SetConsoleCtrlHandler restores the original console modes / codepage /
//      alt-screen on Ctrl+C, Ctrl+Break or window close. Without this the
//      terminal is left with raw input mode, a hidden cursor and no scrollbar.
//  12. Guards for invalid console handles, degenerate window sizes and
//      failed GetConsoleScreenBufferInfo / resize.
//  13. Added NTDDI_VERSION so ConPTY APIs (HPCON / CreatePseudoConsole) are
//      visible on both MSVC and MinGW-w64, and explicit <stdarg.h>.
// ---------------------------------------------------------------------------

#define _WIN32_WINNT 0x0A00
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006   // Win10 1809 (RS5) - ConPTY requirement
#endif
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <process.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#endif

#define MAX_PANES         16
#define SCROLL_BUF_LINES  10000
#define READ_BUF_SIZE     8192

// VT Parser States
enum {
    ST_NORMAL = 0,
    ST_ESC,           // ESC received
    ST_ESC_INTER,     // ESC intermediate bytes
    ST_CSI_ENTRY,     // CSI entry
    ST_CSI_PARAM,     // CSI parameters
    ST_CSI_INTER,     // CSI intermediate
    ST_CSI_IGNORE,    // CSI ignore rest
    ST_OSC_STRING,    // OSC string
    ST_DCS_ENTRY,     // DCS entry
    ST_DCS_PARAM,     // DCS parameters
    ST_DCS_INTER,     // DCS intermediate
    ST_DCS_PASSTHROUGH, // DCS passthrough
    ST_DCS_IGNORE,    // DCS ignore
    ST_SOS_STRING,    // SOS/PM/APC string
};

typedef struct {
    CHAR_INFO *buffer;
    int cols, rows, total_lines, scroll_top;
    int cursor_x, cursor_y, cursor_visible;
    WORD current_attr;
    int fg_color, bg_color, bold, underline, reverse_video;

    // VT parser state
    int state;
    char param_buf[256];
    int param_len;
    char inter_buf[16];
    int inter_len;
    int osc_num;
    char osc_buf[512];
    int osc_len;
    int osc_sep;   // v8.9: 1 = the ';' separator was seen; chars after it are title

    int saved_cx, saved_cy;
    CHAR_INFO *alt_buffer;
    int in_alt_screen, alt_scroll_top;
    int origin_mode, auto_wrap, wraparound_pending;
    int scroll_region_top, scroll_region_bottom;
    int app_cursor_keys, app_keypad;
    int mouse_tracking, mouse_sgr, bracketed_paste, win32_input_mode;
    char tab_stops[512];
    char response_buf[256];
    int response_len;

    // v7: per-pane UTF-8 decoder state (was a shared static -> cross-pane corruption)
    unsigned utf8_state, utf8_cp;
    // v7: which pane owns this screen (used by execute_osc for the title)
    int pane_index;
    // v8.3: ConPTY line-width autodetect. ConPTY emits every "screen row" as a
    // continuous stream padded with spaces to ITS window width (e.g. legacy
    // full-screen apps such as edit.com change the console window to 134
    // columns without ConPTY sending "CSI 8t" - a known ConPTY gap). If our
    // model is wider than ConPTY's, the next row's content lands mid-row and
    // every line shifts (staircase misalignment). We detect the real width by
    // spotting that "content was packed onto the tail of a padded row".
    int detect_col, detect_count;

    // v8.7: 24-bit true color. ConPTY sends truecolor (38;2;r;g;b / 48;2;r;g;b)
    // which edit.com uses for its blue menu bar / gray edit area. The 16-color
    // quantization in build_attr crushed everything dark to black, so the
    // colors disappeared. We now keep the current truecolor fg/bg and store a
    // per-cell RGB565 copy (parallel arrays, same size as buffer/alt_buffer).
    // Bit 15 = 1 means "no truecolor for this cell" (fall back to 16-color).
    int fg_r, fg_g, fg_b, bg_r, bg_g, bg_b;   // current truecolor (0-255)
    int fg_rgb_on, bg_rgb_on;                 // is current color truecolor?
    // v8.8: per-cell truecolor is stored as RGB565 in fg_rgb/bg_rgb plus a
    // VALID flag in rgb_valid (bit0=fg, bit1=bg). A sentinel value cannot be
    // used because EVERY 16-bit RGB565 value is a real color - notably
    // RGB565(255,255,255) == 0xFFFF, which collided with the old sentinel and
    // silently dropped white foregrounds (edit.com's menu text).
    WORD *fg_rgb, *bg_rgb;                    // per-cell RGB565 (always valid value)
    WORD *alt_fg_rgb, *alt_bg_rgb;            // alt-screen per-cell RGB565
    unsigned char *rgb_valid, *alt_rgb_valid; // bit0=fg truecolor, bit1=bg truecolor
    // v8.13: how many lines have actually scrolled out of the top of the
    // screen (real scrollback depth). scroll_top itself is constant
    // (== SCROLL_BUF_LINES) because the buffer is used as a ring, so it can't
    // limit scrolling; without this, do_scroll allowed scrolling into empty
    // buffer when there was no history at all.
    int hist_lines;
    // v8.16: scrollback depth saved when entering the alternate screen. Full-
    // screen apps (nano, vim...) run in the alt screen and clear it with
    // "CSI 2J" on startup - if that also cleared hist_lines, the user's
    // scrollback was wiped (truncated to just the visible rows) after the app
    // exited. Save/restore it around alt-screen switches.
    int alt_hist_lines;
} ScreenBuffer;

#define RGB565_WHITE 0xFFFF    // RGB565 encoding of (255,255,255)
#define RGB565_BLACK 0x0000
static inline WORD rgb565(int r, int g, int b) {
    return (WORD)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static inline void rgb565_split(WORD v, int *r, int *g, int *b) {
    *r = (v >> 11) & 0x1F; *r = (*r << 3) | (*r >> 2);
    *g = (v >> 5) & 0x3F; *g = (*g << 2) | (*g >> 4);
    *b = v & 0x1F; *b = (*b << 3) | (*b >> 2);
}

typedef struct {
    int start_col, end_col, pane_idx;
    int close_start, close_end;   // v8.10: column range of the 'x' close button (pane_idx >= 0)
} PaneTabInfo;

typedef struct {
    int active;
    HPCON hpc;
    HANDLE pipe_in, pipe_out, process, thread, read_thread;
    ScreenBuffer screen;
    char title[32];
    int scroll_offset;
    int color;   // v8.32: tab color index 0=default, 1..8 = palette
    int exited_hold; // 1 = process exited with error, holding screen for user
    DWORD exit_code;
    WCHAR input_history[256];
    int input_history_len;
    int input_history_pos;
} Pane;

typedef struct {
    Pane panes[MAX_PANES];
    int pane_count, active_pane;
    volatile LONG running;
    int host_cols, host_rows, total_host_rows;
    HANDLE hOut, hIn;
    CRITICAL_SECTION cs;
    int needs_redraw, prefix_mode;
    int help_mode;   // v8.18: show the built-in help view (no child process)
    int help_scroll; // v8.19: scroll offset inside the help view (PgUp/PgDn/wheel)
    int chooser_mode; // v8.21: [+] clicked - choose cmd or powershell
    int custom_cmd_mode; // custom command input mode
    char custom_cmd_buf[128];
    int custom_cmd_len;
    int custom_cmd_pos;
    int ctx_mode;     // v8.33: right-click context menu (0=off, 1=menu, 2=color picker)
    int ctx_pane;     // v8.33: pane the context menu targets
    int rename_mode;  // v8.33: typing a new title (keyboard input)
    char rename_buf[64];
    int rename_len;
    int rename_pos;
    int settings_mode;       // 0=off, 1=main settings, 2=edit item, 3=presets
    int settings_sel;        // selected item index 0..N-1
    int settings_edit_idx;   // editing item index (or -1 for new)
    int settings_edit_field; // 0=name, 1=cmd
    char settings_edit_name[32];
    int settings_edit_name_len;
    int settings_edit_name_pos;
    char settings_edit_cmd[256];
    int settings_edit_cmd_len;
    int settings_edit_cmd_pos;
    DWORD orig_in_mode, orig_out_mode;
    UINT orig_cp, orig_input_cp;
    PaneTabInfo tab_info[MAX_PANES + 3];
    int tab_count;
} Multiplexer;

static Multiplexer g_mux;
// v8.11: last known mouse position, used for hover highlighting of the tab bar
static int g_mouse_x = -1, g_mouse_y = -1;
// v8.26: was the mouse last on the tab bar row? Used to trigger a redraw when
// the mouse LEAVES the tab bar, so hover highlights don't stick.
static int g_mouse_prev_in_tabbar = 0;
// v8.45: anchor column captured when a popup (ctx menu / chooser) opens, so
// the popup stays put even if the mouse keeps moving afterwards.
static int g_pop_anchor_x = -1;
static WCHAR g_orig_title[256] = {0};
static char g_current_host_title[128] = {0};

typedef struct {
    char name[32];
    char cmd[256];
} ChooserItem;

#define MAX_CHOOSER_ITEMS 9
static ChooserItem g_chooser_items[MAX_CHOOSER_ITEMS];
static int g_chooser_item_count = 0;

static void init_default_config(void) {
    g_chooser_item_count = 3;
    strcpy(g_chooser_items[0].name, "cmd");
    strcpy(g_chooser_items[0].cmd, "cmd.exe");

    strcpy(g_chooser_items[1].name, "PowerShell");
    strcpy(g_chooser_items[1].cmd, "powershell.exe");

    strcpy(g_chooser_items[2].name, "自定义命令行");
    strcpy(g_chooser_items[2].cmd, ":custom");
}

static void load_config(void) {
    init_default_config();

    WCHAR exe_path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    WCHAR *last_bs = wcsrchr(exe_path, L'\\');
    WCHAR ini_path[MAX_PATH] = {0};
    if (last_bs) {
        *last_bs = 0;
        _snwprintf(ini_path, MAX_PATH - 1, L"%s\\termux.ini", exe_path);
    } else {
        wcscpy(ini_path, L"termux.ini");
    }

    FILE *f = _wfopen(ini_path, L"rb");
    if (!f) {
        const WCHAR *prof = _wgetenv(L"USERPROFILE");
        if (prof) {
            WCHAR user_ini[MAX_PATH] = {0};
            _snwprintf(user_ini, MAX_PATH - 1, L"%s\\.termux.ini", prof);
            f = _wfopen(user_ini, L"rb");
        }
    }

    // If config file doesn't exist, create a default termux.ini with helpful comments
    if (!f) {
        FILE *wf = _wfopen(ini_path, L"wb");
        if (wf) {
            const char *default_ini =
                "# win-termux 配置文件 (UTF-8)\r\n"
                "# 可自定义 [+] 新建菜单中的条目、位置顺序与启动命令（支持 1-9 项）\r\n"
                "# 格式: 序号 = 菜单显示名称, 启动命令行\r\n"
                "# 特殊命令 \":custom\" 表示打开自定义命令行输入框\r\n"
                "\r\n"
                "[menu]\r\n"
                "1 = cmd, cmd.exe\r\n"
                "2 = PowerShell, powershell.exe\r\n"
                "3 = 自定义命令行, :custom\r\n"
                "# 示例（取消注释即可启用）:\r\n"
                "# 4 = WSL, wsl.exe\r\n"
                "# 5 = Git Bash, \"C:\\Program Files\\Git\\bin\\bash.exe\" --login -i\r\n"
                "# 6 = Python, python -i\r\n";
            fwrite(default_ini, 1, strlen(default_ini), wf);
            fclose(wf);
        }
        return;
    }

    char line[512];
    int parsed_count = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#' || *p == ';' || *p == '\r' || *p == '\n' || *p == '[') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        char *val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;
        char *comma = strchr(val, ',');
        if (!comma) continue;
        *comma = 0;
        char *name = val;
        char *cmd = comma + 1;
        while (*cmd == ' ' || *cmd == '\t') cmd++;

        // trim trailing whitespace from name
        int nlen = (int)strlen(name);
        while (nlen > 0 && ((unsigned char)name[nlen - 1] <= ' ' || name[nlen - 1] == '\r' || name[nlen - 1] == '\n')) name[--nlen] = 0;

        // trim trailing whitespace from cmd
        int clen = (int)strlen(cmd);
        while (clen > 0 && ((unsigned char)cmd[clen - 1] <= ' ' || cmd[clen - 1] == '\r' || cmd[clen - 1] == '\n')) cmd[--clen] = 0;

        if (nlen > 0 && clen > 0 && parsed_count < MAX_CHOOSER_ITEMS) {
            strncpy(g_chooser_items[parsed_count].name, name, sizeof(g_chooser_items[0].name) - 1);
            g_chooser_items[parsed_count].name[sizeof(g_chooser_items[0].name) - 1] = 0;

            strncpy(g_chooser_items[parsed_count].cmd, cmd, sizeof(g_chooser_items[0].cmd) - 1);
            g_chooser_items[parsed_count].cmd[sizeof(g_chooser_items[0].cmd) - 1] = 0;
            parsed_count++;
        }
    }
    fclose(f);

    if (parsed_count > 0) {
        g_chooser_item_count = parsed_count;
    }
}

static const ChooserItem g_presets[] = {
    {"cmd", "cmd.exe"},
    {"PowerShell", "powershell.exe"},
    {"WSL", "wsl.exe"},
    {"Git Bash", "\"C:\\Program Files\\Git\\bin\\bash.exe\" --login -i"},
    {"Python", "python -i"},
    {"Node.js", "node"},
    {"自定义命令行", ":custom"},
};
static const int g_preset_count = (int)(sizeof(g_presets) / sizeof(g_presets[0]));

static void save_config(void) {
    WCHAR exe_path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    WCHAR *last_bs = wcsrchr(exe_path, L'\\');
    WCHAR ini_path[MAX_PATH] = {0};
    if (last_bs) {
        *last_bs = 0;
        _snwprintf(ini_path, MAX_PATH - 1, L"%s\\termux.ini", exe_path);
    } else {
        wcscpy(ini_path, L"termux.ini");
    }

    FILE *f = _wfopen(ini_path, L"wb");
    if (!f) {
        const WCHAR *prof = _wgetenv(L"USERPROFILE");
        if (prof) {
            WCHAR user_ini[MAX_PATH] = {0};
            _snwprintf(user_ini, MAX_PATH - 1, L"%s\\.termux.ini", prof);
            f = _wfopen(user_ini, L"wb");
        }
    }
    if (!f) return;

    const char *header =
        "# win-termux 配置文件 (UTF-8)\r\n"
        "# 可自定义 [+] 新建菜单中的条目、位置顺序与启动命令（支持 1-9 项）\r\n"
        "# 格式: 序号 = 菜单显示名称, 启动命令行\r\n"
        "# 特殊命令 \":custom\" 表示打开自定义命令行输入框\r\n"
        "\r\n"
        "[menu]\r\n";
    fwrite(header, 1, strlen(header), f);

    for (int i = 0; i < g_chooser_item_count; i++) {
        char line[512];
        int len = snprintf(line, sizeof(line), "%d = %s, %s\r\n", i + 1, g_chooser_items[i].name, g_chooser_items[i].cmd);
        if (len > 0) fwrite(line, 1, len, f);
    }
    fclose(f);
}

// Rendering happens frequently while pane output is streaming. Reuse one
// scratch buffer instead of allocating and freeing a multi-megabyte block on
// every frame; render_screen is serialized by g_mux.cs, so one buffer is
// sufficient for every view.
static char *g_render_buf = NULL;
static int g_render_buf_cap = 0;

static char *render_buffer_acquire(int needed) {
    if (needed <= 0) return NULL;
    if (g_render_buf_cap >= needed) return g_render_buf;
    int cap = g_render_buf_cap > 0 ? g_render_buf_cap : 16384;
    while (cap < needed) {
        if (cap > 0x3FFFFFFF) { cap = needed; break; }
        cap *= 2;
    }
    char *next = (char *)realloc(g_render_buf, (size_t)cap);
    if (!next) return NULL;
    g_render_buf = next;
    g_render_buf_cap = cap;
    return g_render_buf;
}

static void write_to_pane(const char *data, int len);
static void draw_tab_bar(char *out, int bs, int *posp);   // v8.18
static void render_help_content(char *out, int bs, int *posp, int host_rows, int host_cols);   // v8.18
static void render_ctx_menu(char *out, int bs, int *posp, int host_rows, int host_cols);   // v8.33
static void render_color_picker(char *out, int bs, int *posp, int host_rows, int host_cols);   // v8.33
static void close_pane(int idx);
static void switch_pane(int idx);
static int create_pane(void);   // v8.17: used by open_help_pane
static int create_pane_shell(const WCHAR *shell);   // v8.21
static int screen_resize(ScreenBuffer *s, int nc, int nr);   // used by execute_csi (CSI 8t)

static inline unsigned int utf8_decode_cp(const char *s, int max_len, int *adv) {
    if (max_len <= 0) { *adv = 0; return 0; }
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) { *adv = 1; return c; }
    if ((c & 0xE0) == 0xC0 && max_len >= 2) {
        *adv = 2;
        return ((c & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && max_len >= 3) {
        *adv = 3;
        return ((c & 0x0F) << 12) | (((unsigned char)s[1] & 0x3F) << 6) | ((unsigned char)s[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && max_len >= 4) {
        *adv = 4;
        return ((c & 0x07) << 18) | (((unsigned char)s[1] & 0x3F) << 12) | (((unsigned char)s[2] & 0x3F) << 6) | ((unsigned char)s[3] & 0x3F);
    }
    *adv = 1;
    return c;
}

static inline int is_zero_width_cp(unsigned int cp) {
    if (cp >= 0xFE00 && cp <= 0xFE0F) return 1;       // Variation Selectors (e.g. FE0F in emoji)
    if (cp == 0x200D) return 1;                       // Zero-Width Joiner (ZWJ)
    if (cp >= 0x0300 && cp <= 0x036F) return 1;       // Combining Diacritical Marks
    if (cp >= 0x1AB0 && cp <= 0x1AFF) return 1;
    if (cp >= 0x1DC0 && cp <= 0x1DFF) return 1;
    if (cp >= 0x20D0 && cp <= 0x20FF) return 1;       // Combining Diacritical Marks for Symbols (Keycaps 0x20E3)
    if (cp >= 0xFE20 && cp <= 0xFE2F) return 1;
    if (cp >= 0xE0020 && cp <= 0xE007F) return 1;     // Tag characters
    if (cp >= 0xE0100 && cp <= 0xE01EF) return 1;     // Variation Selectors Supplement
    if (cp >= 0x1F3FB && cp <= 0x1F3FF) return 1;     // Emoji skin tone modifiers
    return 0;
}

static inline int is_wide_cp(unsigned int cp) {
    if (cp < 0x1100) return 0;
    if (cp <= 0x115F) return 1;                         // Hangul Jamo
    if (cp == 0x231A || cp == 0x231B) return 1;         // ⌚, ⌛
    if (cp >= 0x23E9 && cp <= 0x23EC) return 1;         // ⏩..⏬
    if (cp == 0x23F0 || cp == 0x23F3) return 1;         // ⏰, ⏳
    if (cp >= 0x25FD && cp <= 0x25FE) return 1;         // ◽, ◾
    if (cp >= 0x2614 && cp <= 0x2615) return 1;         // ☔, ☕
    if (cp >= 0x2648 && cp <= 0x2653) return 1;         // ♈..♓
    if (cp == 0x267F || cp == 0x2693 || cp == 0x26A1) return 1; // ♿, ⚓, ⚡
    if (cp >= 0x26AA && cp <= 0x26AB) return 1;         // ⚪, ⚫
    if (cp >= 0x26BD && cp <= 0x26BE) return 1;         // ⚽, ⚾
    if (cp >= 0x26C4 && cp <= 0x26C5) return 1;         // ⛄, ⛅
    if (cp == 0x26CE || cp == 0x26D4 || cp == 0x26EA) return 1; // ⛎, ⛔, ⛪
    if (cp >= 0x26F2 && cp <= 0x26F3) return 1;         // ⛲, ⛳
    if (cp == 0x26F5 || cp == 0x26FA || cp == 0x26FD) return 1; // ⛵, ⛺, ⛽
    if (cp == 0x2705) return 1;                         // ✅
    if (cp >= 0x270A && cp <= 0x270B) return 1;         // ✊, ✋
    if (cp == 0x2728) return 1;                         // ✨
    if (cp == 0x274C || cp == 0x274E) return 1;         // ❌, ❎
    if (cp >= 0x2753 && cp <= 0x2755) return 1;         // ❓..❕
    if (cp == 0x2757) return 1;                         // ❗
    if (cp >= 0x2795 && cp <= 0x2797) return 1;         // ➕..➗
    if (cp == 0x27B0 || cp == 0x27BF) return 1;         // ➰, ➿
    if (cp >= 0x2B1B && cp <= 0x2B1C) return 1;         // ⬛, ⬜
    if (cp == 0x2B50 || cp == 0x2B55) return 1;         // ⭐, ⭕
    if (cp >= 0x2E80 && cp <= 0xA4C6) return 1;         // CJK radicals, Kangxi, Hiragana, Katakana, Bopomofo, CJK ideographs, Yi
    if (cp >= 0xA960 && cp <= 0xA97C) return 1;         // Hangul Jamo Extended-A
    if (cp >= 0xAC00 && cp <= 0xD7A3) return 1;         // Hangul Syllables
    if (cp >= 0xF900 && cp <= 0xFAFF) return 1;         // CJK Compatibility Ideographs
    if (cp >= 0xFE10 && cp <= 0xFE19) return 1;         // Vertical forms
    if (cp >= 0xFE30 && cp <= 0xFE6B) return 1;         // CJK Compatibility Forms, small forms
    if (cp >= 0xFF01 && cp <= 0xFF60) return 1;         // Fullwidth forms
    if (cp >= 0xFFE0 && cp <= 0xFFE6) return 1;         // Fullwidth symbols
    if (cp >= 0x1F000 && cp <= 0x1FFFF) return 1;       // SMP Emoji (😀, 🎉, etc.)
    return 0;
}

static inline int is_wide_char(WCHAR c) {
    return is_wide_cp((unsigned int)c);
}

static inline int is_ri(unsigned int cp) {
    return (cp >= 0x1F1E6 && cp <= 0x1F1FF); // Regional Indicator Symbol
}

static inline int is_combining_or_modifier(unsigned int cp) {
    if (is_zero_width_cp(cp)) return 1;
    return 0;
}

// v8.11: terminal column width of a UTF-8 string (wide chars = 2 cols, zero-width = 0 cols).
static int utf8_cols(const char *s, int len) {
    int cols = 0, i = 0;
    while (i < len) {
        int adv = 0;
        unsigned int cp = utf8_decode_cp(s + i, len - i, &adv);
        if (is_zero_width_cp(cp)) {
            // zero width
        } else if (is_wide_cp(cp)) {
            cols += 2;
        } else {
            cols += 1;
        }
        i += adv;
    }
    return cols;
}

static void append_padded_utf8(char *out, int bs, int *posp, int *colsp, const char *s, int target_cols) {
    int pos = *posp;
    int len = (int)strlen(s);
    int cols = 0;
    int i = 0;
    while (i < len) {
        int adv = 0;
        unsigned int cp = utf8_decode_cp(s + i, len - i, &adv);
        if (is_zero_width_cp(cp)) {
            for (int k = 0; k < adv && pos < bs - 1; k++) out[pos++] = s[i + k];
            i += adv;
            continue;
        }
        int w = is_wide_cp(cp) ? 2 : 1;
        if (cols + w > target_cols) break;
        for (int k = 0; k < adv && pos < bs - 1; k++) out[pos++] = s[i + k];
        cols += w;
        i += adv;
    }
    while (cols < target_cols && pos < bs - 1) {
        out[pos++] = ' ';
        cols++;
    }
    *posp = pos;
    if (colsp) *colsp += cols;
}

static void pad_to_right_border(char *out, int bs, int *posp, int *colsp, int target_w) {
    int pos = *posp;
    int cols = *colsp;
    while (cols < target_w - 1 && pos < bs - 8) {
        out[pos++] = ' ';
        cols++;
    }
    pos += snprintf(out + pos, bs - pos, "\x1b[0m\x1b[48;2;33;38;45m│\x1b[0m");
    cols++;
    *posp = pos;
    *colsp = cols;
}

static int utf8_next_grapheme(const char *buf, int len, int pos) {
    if (pos >= len) return len;
    int p = pos;
    int adv = 0;
    unsigned int cp = utf8_decode_cp(buf + p, len - p, &adv);
    p += adv;

    // CRLF
    if (cp == 0x0D && p < len && (unsigned char)buf[p] == 0x0A) {
        return p + 1;
    }

    // Regional Indicator pair (e.g. flags)
    if (is_ri(cp)) {
        if (p < len) {
            int nadv = 0;
            unsigned int next_cp = utf8_decode_cp(buf + p, len - p, &nadv);
            if (is_ri(next_cp)) {
                p += nadv;
                return p;
            }
        }
    }

    unsigned int prev_cp = cp;
    while (p < len) {
        int next_adv = 0;
        unsigned int next_cp = utf8_decode_cp(buf + p, len - p, &next_adv);
        if (prev_cp == 0x200D) {
            // After ZWJ, consume the joined character
            p += next_adv;
            prev_cp = next_cp;
            continue;
        }
        if (is_combining_or_modifier(next_cp)) {
            p += next_adv;
            prev_cp = next_cp;
            continue;
        }
        break;
    }
    return p;
}

static int utf8_prev_grapheme(const char *buf, int pos) {
    if (pos <= 0) return 0;
    int cur = 0;
    while (cur < pos) {
        int next = utf8_next_grapheme(buf, pos, cur);
        if (next >= pos) return cur;
        cur = next;
    }
    return 0;
}

static void buf_insert_utf8(char *buf, int *len, int *pos, int max_cap, const char *utf8_bytes, int byte_count) {
    if (*len + byte_count >= max_cap) return;
    if (*pos < *len) {
        memmove(buf + *pos + byte_count, buf + *pos, *len - *pos);
    }
    memcpy(buf + *pos, utf8_bytes, byte_count);
    *len += byte_count;
    *pos += byte_count;
    buf[*len] = 0;
}

static void buf_backspace(char *buf, int *len, int *pos) {
    if (*pos <= 0 || *len <= 0) return;
    int prev_p = utf8_prev_grapheme(buf, *pos);
    int del_count = *pos - prev_p;
    if (del_count > 0) {
        if (*pos < *len) {
            memmove(buf + prev_p, buf + *pos, *len - *pos);
        }
        *len -= del_count;
        *pos = prev_p;
        buf[*len] = 0;
    }
}

static void buf_delete(char *buf, int *len, int *pos) {
    if (*pos >= *len || *len <= 0) return;
    int next_p = utf8_next_grapheme(buf, *len, *pos);
    int del_count = next_p - *pos;
    if (del_count > 0) {
        if (next_p < *len) {
            memmove(buf + *pos, buf + next_p, *len - next_p);
        }
        *len -= del_count;
        buf[*len] = 0;
    }
}

// Convert UTF-16 WCHAR buffer to UTF-8
static int wchars_to_utf8(const WCHAR *wbuf, int wlen, char *u8buf, int max_u8) {
    int u8pos = 0;
    for (int i = 0; i < wlen && u8pos < max_u8 - 4; i++) {
        WCHAR uc = wbuf[i];
        if (uc >= 0xD800 && uc <= 0xDBFF && i + 1 < wlen && wbuf[i+1] >= 0xDC00 && wbuf[i+1] <= 0xDFFF) {
            unsigned int cp = 0x10000 + (((unsigned int)(uc & 0x3FF)) << 10) + (wbuf[i+1] & 0x3FF);
            u8buf[u8pos++] = (char)(0xF0 | (cp >> 18));
            u8buf[u8pos++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            u8buf[u8pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            u8buf[u8pos++] = (char)(0x80 | (cp & 0x3F));
            i++;
        } else if (uc < 0x80) {
            u8buf[u8pos++] = (char)uc;
        } else if (uc < 0x800) {
            u8buf[u8pos++] = (char)(0xC0 | (uc >> 6));
            u8buf[u8pos++] = (char)(0x80 | (uc & 0x3F));
        } else {
            u8buf[u8pos++] = (char)(0xE0 | (uc >> 12));
            u8buf[u8pos++] = (char)(0x80 | ((uc >> 6) & 0x3F));
            u8buf[u8pos++] = (char)(0x80 | (uc & 0x3F));
        }
    }
    u8buf[u8pos] = 0;
    return u8pos;
}

// Convert WCHAR index in wbuf to UTF-8 byte index
static int wchar_idx_to_u8_idx(const WCHAR *wbuf, int wlen, int target_widx) {
    if (target_widx <= 0) return 0;
    int u8pos = 0;
    for (int i = 0; i < wlen && i < target_widx; i++) {
        WCHAR uc = wbuf[i];
        if (uc >= 0xD800 && uc <= 0xDBFF && i + 1 < wlen && wbuf[i+1] >= 0xDC00 && wbuf[i+1] <= 0xDFFF) {
            u8pos += 4;
            i++;
        } else if (uc < 0x80) {
            u8pos += 1;
        } else if (uc < 0x800) {
            u8pos += 2;
        } else {
            u8pos += 3;
        }
    }
    return u8pos;
}

// Convert UTF-8 byte index in u8buf to WCHAR index in wbuf
static int u8_idx_to_wchar_idx(const WCHAR *wbuf, int wlen, int target_u8idx) {
    if (target_u8idx <= 0) return 0;
    int u8pos = 0;
    for (int i = 0; i < wlen; i++) {
        if (u8pos >= target_u8idx) return i;
        WCHAR uc = wbuf[i];
        if (uc >= 0xD800 && uc <= 0xDBFF && i + 1 < wlen && wbuf[i+1] >= 0xDC00 && wbuf[i+1] <= 0xDFFF) {
            u8pos += 4;
            i++;
        } else if (uc < 0x80) {
            u8pos += 1;
        } else if (uc < 0x800) {
            u8pos += 2;
        } else {
            u8pos += 3;
        }
    }
    return wlen;
}

// Get count of WCHARs in the grapheme before pos
static int get_prev_grapheme_wchars(const WCHAR *wbuf, int wlen, int pos) {
    if (pos <= 0) return 1;
    char u8[1024];
    wchars_to_utf8(wbuf, wlen, u8, (int)sizeof(u8));
    int cur_u8 = wchar_idx_to_u8_idx(wbuf, wlen, pos);
    int prev_u8 = utf8_prev_grapheme(u8, cur_u8);
    int prev_widx = u8_idx_to_wchar_idx(wbuf, wlen, prev_u8);
    int diff = pos - prev_widx;
    return diff > 0 ? diff : 1;
}

// Get count of WCHARs in the grapheme after pos
static int get_next_grapheme_wchars(const WCHAR *wbuf, int wlen, int pos) {
    if (pos >= wlen) return 1;
    char u8[1024];
    int u8len = wchars_to_utf8(wbuf, wlen, u8, (int)sizeof(u8));
    int cur_u8 = wchar_idx_to_u8_idx(wbuf, wlen, pos);
    int next_u8 = utf8_next_grapheme(u8, u8len, cur_u8);
    int next_widx = u8_idx_to_wchar_idx(wbuf, wlen, next_u8);
    int diff = next_widx - pos;
    return diff > 0 ? diff : 1;
}

static WCHAR g_high_surrogate = 0;

// v8.11: copy up to max-1 bytes of a UTF-8 string without splitting a
// multi-byte character (so the title never shows a broken glyph).
static void trunc_utf8(char *dst, const char *src, int max) {
    int out = 0, i = 0;
    while (src[i] && out < max - 1) {
        unsigned char c = (unsigned char)src[i];
        int adv = 1;
        if ((c & 0xE0) == 0xC0) adv = 2;
        else if ((c & 0xF0) == 0xE0) adv = 3;
        else if ((c & 0xF8) == 0xF0) adv = 4;
        if (out + adv > max - 1) break;
        memcpy(dst + out, src + i, adv);
        out += adv; i += adv;
    }
    dst[out] = 0;
}

// ============================================================
// Console helpers
// ============================================================
static void host_write(const char *s, int len) {
    // WriteConsole may legally complete a short write. Keep advancing so a
    // large rendered frame cannot be silently truncated.
    while (len > 0) {
        DWORD written = 0;
        DWORD chunk = (DWORD)len;
        if (!WriteConsoleA(g_mux.hOut, s, chunk, &written, NULL) || written == 0) break;
        s += written;
        len -= (int)written;
    }
}

static void host_printf(const char *fmt, ...) {
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    // vsnprintf returns the size that would have been written. Clamp it to the
    // bytes actually present instead of reading past buf on truncation.
    if (len > 0) {
        if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
        host_write(buf, len);
    }
}

// v8.2: optional raw ConPTY byte dump for diagnosing display bugs
// (set TERMUX_DUMP=1 before running). Writes every byte read from each pane
// into termux_dump.log with a [pane N] header per read chunk.
static int g_dump_enabled = 0;
static void dump_pane_bytes(int idx, const char *data, int len) {
    if (!g_dump_enabled || len <= 0) return;
    FILE *f = fopen("termux_dump.log", "ab");
    if (!f) return;
    fprintf(f, "[pane %d len %d]\n", idx, len);
    fwrite(data, 1, len, f);
    fputc('\n', f);
    fclose(f);
}

// v8.5: also dump what WE render to the host terminal, so a display bug can be
// traced end-to-end: compare termux_dump.log (ConPTY -> us) against
// render_dump.log (us -> host terminal).
static void dump_render_output(const char *data, int len, int mcols, int mrows, int hcols, int hrows) {
    if (!g_dump_enabled || len <= 0) return;
    FILE *f = fopen("render_dump.log", "ab");
    if (!f) return;
    fprintf(f, "[render len %d model %dx%d host %dx%d]\n", len, mcols, mrows, hcols, hrows);
    fwrite(data, 1, len, f);
    fputc('\n', f);
    fclose(f);
}

// v8.54: mouse-event diagnostics (TERMUX_DUMP=1) written to mouse_dump.log.
// Unlike v8.53 (which only logged presses on specific paths), EVERY mouse
// event is logged so any interaction is traceable. Moves are throttled
// (1 in 20); presses/releases/double-clicks are always logged. main() also
// writes a "[v8.54] startup" marker so a fresh log instantly proves which
// binary was running - the user twice reported "no mouse_dump.log", which
// turned out to be an OLD exe still running (old logs had no [v8.5x] prefix).
static int g_mouse_log_moves = 0;
static void log_mouse_event(const char *tag, MOUSE_EVENT_RECORD *me) {
    if (!g_dump_enabled) return;
    unsigned btn = (unsigned)me->dwButtonState;
    int is_press = (btn & (FROM_LEFT_1ST_BUTTON_PRESSED | FROM_LEFT_2ND_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED)) &&
                   (me->dwEventFlags == 0 || me->dwEventFlags == DOUBLE_CLICK);
    int is_release = (btn & 0x7) == 0 && me->dwEventFlags == 0;
    if (me->dwEventFlags == MOUSE_MOVED) {
        if (++g_mouse_log_moves < 20) return;   // throttle pure moves
        g_mouse_log_moves = 0;
    }
    if (me->dwEventFlags == MOUSE_WHEELED || me->dwEventFlags == MOUSE_HWHEELED) return;   // too noisy
    FILE *f = fopen("mouse_dump.log", "ab");
    if (!f) return;
    fprintf(f, "[v8.54] %s pos=%d,%d flags=%u btn=0x%X ctrl=0x%X%s | chooser=%d ctx=%d rename=%d help=%d pop_anchor=%d mouse=%d,%d tab_count=%d\n",
            tag, (int)me->dwMousePosition.X, (int)me->dwMousePosition.Y,
            (unsigned)me->dwEventFlags, btn, (unsigned)me->dwControlKeyState,
            is_press ? " PRESS" : (is_release ? " RELEASE" : ""),
            g_mux.chooser_mode, g_mux.ctx_mode, g_mux.rename_mode, g_mux.help_mode,
            g_pop_anchor_x, g_mouse_x, g_mouse_y, g_mux.tab_count);
    if (is_press)
        for (int i = 0; i < g_mux.tab_count; i++) {
            PaneTabInfo *t = &g_mux.tab_info[i];
            fprintf(f, "  tab[%d] pane=%d cols[%d,%d) close[%d,%d)\n", i, t->pane_idx,
                    t->start_col, t->end_col, t->close_start, t->close_end);
        }
    fclose(f);
}

// ============================================================
// Screen buffer
// ============================================================
// Returns 1 on success, 0 if allocation failed (caller must not use the screen).
static int screen_init(ScreenBuffer *s, int cols, int rows) {
    memset(s, 0, sizeof(*s));
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    s->cols = cols;
    s->rows = rows;
    s->total_lines = rows + SCROLL_BUF_LINES;
    s->buffer = (CHAR_INFO *)calloc(s->total_lines * cols, sizeof(CHAR_INFO));
    s->alt_buffer = (CHAR_INFO *)calloc(rows * cols, sizeof(CHAR_INFO));
    // v8.7: per-cell truecolor storage (parallel arrays, main + alt screen)
    s->fg_rgb = (WORD *)malloc(s->total_lines * cols * sizeof(WORD));
    s->bg_rgb = (WORD *)malloc(s->total_lines * cols * sizeof(WORD));
    s->alt_fg_rgb = (WORD *)malloc(rows * cols * sizeof(WORD));
    s->alt_bg_rgb = (WORD *)malloc(rows * cols * sizeof(WORD));
    s->rgb_valid = (unsigned char *)calloc(s->total_lines * cols, 1);
    s->alt_rgb_valid = (unsigned char *)calloc(rows * cols, 1);
    if (!s->buffer || !s->alt_buffer || !s->fg_rgb || !s->bg_rgb || !s->alt_fg_rgb || !s->alt_bg_rgb ||
        !s->rgb_valid || !s->alt_rgb_valid) {
        free(s->buffer); free(s->alt_buffer);
        free(s->fg_rgb); free(s->bg_rgb); free(s->alt_fg_rgb); free(s->alt_bg_rgb);
        free(s->rgb_valid); free(s->alt_rgb_valid);
        s->buffer = s->alt_buffer = NULL;
        s->fg_rgb = s->bg_rgb = s->alt_fg_rgb = s->alt_bg_rgb = NULL;
        s->rgb_valid = s->alt_rgb_valid = NULL;
        return 0;
    }
    for (int i = 0; i < s->total_lines * cols; i++) {
        s->fg_rgb[i] = RGB565_WHITE;   // default fg = white
        s->bg_rgb[i] = 0;              // default bg = black
    }
    for (int i = 0; i < rows * cols; i++) {
        s->alt_fg_rgb[i] = RGB565_WHITE;
        s->alt_bg_rgb[i] = 0;
    }
    s->scroll_top = s->total_lines - rows;
    s->cursor_visible = 1;
    s->current_attr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    s->fg_color = 7;
    s->auto_wrap = 1;
    s->scroll_region_bottom = rows - 1;

    for (int i = 0; i < s->total_lines * cols; i++) {
        s->buffer[i].Char.UnicodeChar = L' ';
        s->buffer[i].Attributes = s->current_attr;
    }
    for (int i = 0; i < rows * cols; i++) {
        s->alt_buffer[i].Char.UnicodeChar = L' ';
        s->alt_buffer[i].Attributes = s->current_attr;
    }
    for (int i = 0; i < cols && i < 512; i += 8)
        s->tab_stops[i] = 1;
    return 1;
}

static void screen_free(ScreenBuffer *s) {
    free(s->buffer);
    free(s->alt_buffer);
    free(s->fg_rgb);
    free(s->bg_rgb);
    free(s->alt_fg_rgb);
    free(s->alt_bg_rgb);
    free(s->rgb_valid);
    free(s->alt_rgb_valid);
    s->buffer = s->alt_buffer = NULL;
    s->fg_rgb = s->bg_rgb = s->alt_fg_rgb = s->alt_bg_rgb = NULL;
    s->rgb_valid = s->alt_rgb_valid = NULL;
}

static CHAR_INFO *screen_cell(ScreenBuffer *s, int row, int col) {
    if (row < 0 || col < 0 || col >= s->cols) return NULL;
    if (s->in_alt_screen) {
        if (row >= s->rows) return NULL;
        return &s->alt_buffer[row * s->cols + col];
    }
    int abs_row = s->scroll_top + row;
    if (abs_row < 0 || abs_row >= s->total_lines) return NULL;
    return &s->buffer[abs_row * s->cols + col];
}

static WORD build_attr(ScreenBuffer *s) {
    int fg = s->fg_color, bg = s->bg_color;
    if (s->reverse_video) { int t = fg; fg = bg; bg = t; }
    if (s->bold && fg < 8) fg += 8;
    static const WORD map[16] = {0,4,2,6,1,5,3,7,8,12,10,14,9,13,11,15};
    WORD attr = 0;
    if (fg >= 0 && fg < 16) attr |= map[fg]; else attr |= 7;
    if (bg >= 0 && bg < 16) attr |= (map[bg] << 4);
    if (s->underline) attr |= COMMON_LVB_UNDERSCORE;
    return attr;
}

// v8.2: does this character occupy TWO columns in the terminal (CJK etc.)?
// The screen model must store such a character in TWO cells (char + 0-fill),
// exactly like the console buffer does, or every line that contains wide
// characters shifts right relative to the real terminal (this is exactly what
// made edit.com's Chinese menus misalign: our model counted 文件 as 2 columns
// while the real terminal renders them as 4).
static void screen_write_cell(ScreenBuffer *s, int row, int col, WCHAR ch, WORD attr);   // v8.7 fwd decl

// v8.7: scroll up/down keep the per-cell truecolor arrays in sync with the
// character buffer (rows move together, new rows clear to INVALID).
static void screen_scroll_up(ScreenBuffer *s, int top, int bottom, int count) {
    if (count <= 0) return;
    // v7: validate region + clamp count so we never index before the buffer
    if (top < 0) top = 0;
    if (bottom >= s->rows) bottom = s->rows - 1;
    if (bottom < top) return;
    if (count > bottom - top + 1) count = bottom - top + 1;

    if (s->in_alt_screen) {
        for (int i = top; i <= bottom - count; i++) {
            memcpy(&s->alt_buffer[i * s->cols], &s->alt_buffer[(i + count) * s->cols], s->cols * sizeof(CHAR_INFO));
            if (s->alt_fg_rgb) {
                memcpy(&s->alt_fg_rgb[i * s->cols], &s->alt_fg_rgb[(i + count) * s->cols], s->cols * sizeof(WORD));
                memcpy(&s->alt_bg_rgb[i * s->cols], &s->alt_bg_rgb[(i + count) * s->cols], s->cols * sizeof(WORD));
            }
        }
        for (int i = bottom - count + 1; i <= bottom; i++)
            for (int j = 0; j < s->cols; j++)
                screen_write_cell(s, i, j, L' ', s->current_attr);
        return;
    }
    if (top == 0 && bottom == s->rows - 1) {
        // v8.13: count real scrollback depth (clamped to the ring size)
        s->hist_lines += count;
        if (s->hist_lines > SCROLL_BUF_LINES) s->hist_lines = SCROLL_BUF_LINES;
        if (s->scroll_top + s->rows + count <= s->total_lines) {
            for (int i = 0; i < count; i++) {
                int line = s->scroll_top + s->rows + i;
                for (int j = 0; j < s->cols; j++) {
                    s->buffer[line * s->cols + j].Char.UnicodeChar = L' ';
                    s->buffer[line * s->cols + j].Attributes = s->current_attr;
                    if (s->fg_rgb) { s->fg_rgb[line * s->cols + j] = RGB565_WHITE; s->bg_rgb[line * s->cols + j] = RGB565_BLACK; s->rgb_valid[line * s->cols + j] = 0; }
                }
            }
            s->scroll_top += count;
        } else {
            int need = (s->scroll_top + s->rows + count) - s->total_lines;
            if (need > s->scroll_top) need = s->scroll_top;
            if (need > 0) {
                memmove(s->buffer, s->buffer + need * s->cols, (s->total_lines - need) * s->cols * sizeof(CHAR_INFO));
                if (s->fg_rgb) {
                    memmove(s->fg_rgb, s->fg_rgb + need * s->cols, (s->total_lines - need) * s->cols * sizeof(WORD));
                    memmove(s->bg_rgb, s->bg_rgb + need * s->cols, (s->total_lines - need) * s->cols * sizeof(WORD));
                }
                s->scroll_top -= need;
                for (int i = s->total_lines - need; i < s->total_lines; i++)
                    for (int j = 0; j < s->cols; j++) {
                        s->buffer[i * s->cols + j].Char.UnicodeChar = L' ';
                        s->buffer[i * s->cols + j].Attributes = s->current_attr;
                        if (s->fg_rgb) { s->fg_rgb[i * s->cols + j] = RGB565_WHITE; s->bg_rgb[i * s->cols + j] = RGB565_BLACK; s->rgb_valid[i * s->cols + j] = 0; }
                    }
            }
            if (s->scroll_top + s->rows + count <= s->total_lines) {
                for (int i = 0; i < count; i++) {
                    int line = s->scroll_top + s->rows + i;
                    for (int j = 0; j < s->cols; j++) {
                        s->buffer[line * s->cols + j].Char.UnicodeChar = L' ';
                        s->buffer[line * s->cols + j].Attributes = s->current_attr;
                        if (s->fg_rgb) { s->fg_rgb[line * s->cols + j] = RGB565_WHITE; s->bg_rgb[line * s->cols + j] = RGB565_BLACK; s->rgb_valid[line * s->cols + j] = 0; }
                    }
                }
                s->scroll_top += count;
            }
        }
    } else {
        int abs_top = s->scroll_top + top, abs_bottom = s->scroll_top + bottom;
        for (int i = abs_top; i <= abs_bottom - count; i++) {
            memcpy(&s->buffer[i * s->cols], &s->buffer[(i + count) * s->cols], s->cols * sizeof(CHAR_INFO));
            if (s->fg_rgb) {
                memcpy(&s->fg_rgb[i * s->cols], &s->fg_rgb[(i + count) * s->cols], s->cols * sizeof(WORD));
                memcpy(&s->bg_rgb[i * s->cols], &s->bg_rgb[(i + count) * s->cols], s->cols * sizeof(WORD));
            }
        }
        for (int i = abs_bottom - count + 1; i <= abs_bottom; i++)
            for (int j = 0; j < s->cols; j++) {
                s->buffer[i * s->cols + j].Char.UnicodeChar = L' ';
                s->buffer[i * s->cols + j].Attributes = s->current_attr;
                if (s->fg_rgb) { s->fg_rgb[i * s->cols + j] = RGB565_WHITE; s->bg_rgb[i * s->cols + j] = RGB565_BLACK; s->rgb_valid[i * s->cols + j] = 0; }
            }
    }
}

static void screen_scroll_down(ScreenBuffer *s, int top, int bottom, int count) {
    if (count <= 0) return;
    // v7: validate region + clamp count (same reasoning as screen_scroll_up)
    if (top < 0) top = 0;
    if (bottom >= s->rows) bottom = s->rows - 1;
    if (bottom < top) return;
    if (count > bottom - top + 1) count = bottom - top + 1;

    CHAR_INFO *buf = s->in_alt_screen ? s->alt_buffer : s->buffer;
    WORD *fgb = s->in_alt_screen ? s->alt_fg_rgb : s->fg_rgb;
    WORD *bgb = s->in_alt_screen ? s->alt_bg_rgb : s->bg_rgb;
    int base = s->in_alt_screen ? 0 : s->scroll_top;
    int abs_top = base + top, abs_bottom = base + bottom;
    // v8.13: a full-screen scroll down pulls lines back from the top, so the
    // available scrollback depth shrinks.
    if (!s->in_alt_screen && top == 0 && bottom == s->rows - 1) {
        s->hist_lines -= count;
        if (s->hist_lines < 0) s->hist_lines = 0;
    }
    for (int i = abs_bottom; i >= abs_top + count; i--) {
        memcpy(&buf[i * s->cols], &buf[(i - count) * s->cols], s->cols * sizeof(CHAR_INFO));
        if (fgb) {
            memcpy(&fgb[i * s->cols], &fgb[(i - count) * s->cols], s->cols * sizeof(WORD));
            memcpy(&bgb[i * s->cols], &bgb[(i - count) * s->cols], s->cols * sizeof(WORD));
        }
    }
    for (int i = abs_top; i < abs_top + count && i <= abs_bottom; i++)
        for (int j = 0; j < s->cols; j++) {
            buf[i * s->cols + j].Char.UnicodeChar = L' ';
            buf[i * s->cols + j].Attributes = s->current_attr;
            if (fgb) { fgb[i * s->cols + j] = RGB565_WHITE; bgb[i * s->cols + j] = RGB565_BLACK; s->in_alt_screen ? (s->alt_rgb_valid[i * s->cols + j] = 0) : (s->rgb_valid[i * s->cols + j] = 0); }
        }
}

static void screen_newline(ScreenBuffer *s) {
    if (s->cursor_y >= s->scroll_region_bottom)
        screen_scroll_up(s, s->scroll_region_top, s->scroll_region_bottom, 1);
    else if (s->cursor_y < s->rows - 1)
        s->cursor_y++;
}

// v8.3: ConPTY line-width autodetect.
//
// ConPTY streams every screen row as "row content + spaces padded to ITS OWN
// window width W", with no explicit row markers - rows are separated purely by
// wrapping. When a legacy full-screen app (edit.com...) resizes the console
// window, ConPTY uses the new W but (for old APIs) often never sends "CSI 8t",
// so our model keeps its original width. If the model is WIDER than W, the
// next row's first characters land on the tail of the padded row, right after
// its long space-padding run - producing the staircase misalignment.
//
// Detection (called when a row fills up and would wrap):
//   The row looks like:  [content][long space run][packed-in next row...]
//   The FIRST long space run after content ends is ConPTY's padding; the
//   column right after it is ConPTY's real width. Shrink the model to it.
//   (Using the FIRST run - not the last - is what makes this work regardless
//   of how wide the model is: the packed-in content may itself end in spaces,
//   which would otherwise be mistaken for the padding run.)
//   The threshold matters: ConPTY's row padding is huge (90+ spaces: it pads
//   every row to its full window width), while real content rarely has 50+
//   consecutive spaces (edit.com's "About" dialog is preceded by ~24). 50
//   cleanly separates the two.
static void detect_conpty_width(ScreenBuffer *s) {
    // v8.6: DISABLED. The heuristic misfires on edit.com's own status line
    // (which is a padded row followed by more content) and wrongly shrinks the
    // model (120 -> 103), wrecking the layout far worse than the ~1-column
    // offset it tries to fix. Real ConPTY row width tracks the host window
    // width, so trusting the window size is the correct behavior.
    (void)s;
    return;
#if 0   // old implementation (kept for reference)
    if (s->detect_count >= 100) return;   // cooling down after an adapt
    // Scan for the FIRST run of >= 50 spaces in this row.
    int run_start = -1, run_end = -1;
    int x = 0;
    while (x < s->cols) {
        CHAR_INFO *c = screen_cell(s, s->cursor_y, x);
        if (c && c->Char.UnicodeChar == L' ') {
            int st = x;
            while (x < s->cols) {
                CHAR_INFO *c2 = screen_cell(s, s->cursor_y, x);
                if (c2 && c2->Char.UnicodeChar == L' ') x++; else break;
            }
            if (x - st >= 50) { run_start = st; run_end = x - 1; break; }   // first long run
        } else {
            x++;
        }
    }
    if (run_start < 0) { s->detect_count = 0; return; }   // no padding run -> width matches
    // Padding run must be preceded by content and followed by content.
    if (run_start == 0) { s->detect_count = 0; return; }
    CHAR_INFO *pre = screen_cell(s, s->cursor_y, run_start - 1);
    if (!pre || pre->Char.UnicodeChar == L' ' || pre->Char.UnicodeChar == 0) { s->detect_count = 0; return; }
    int mix_start = run_end + 1;
    int has_after = 0, tail_spaces = 0;
    for (int xx = mix_start; xx < s->cols; xx++) {
        CHAR_INFO *c = screen_cell(s, s->cursor_y, xx);
        if (c && c->Char.UnicodeChar != L' ' && c->Char.UnicodeChar != 0) { has_after = 1; }
        else if (c && c->Char.UnicodeChar == L' ') tail_spaces++;
    }
    if (!has_after) { s->detect_count = 0; return; }   // row ends in padding -> width matches
    // Packed-in content should be a reasonable size.
    int packed = s->cols - mix_start;
    if (packed < 2 || packed > 300) {
        // < 2: a single trailing glyph is usually the app's cursor block
        // (edit.com draws a filled block after an \x1b[K erase, which would
        // otherwise look like a 1-column "packed-in" row and trigger a bogus
        // resize). A real ConPTY over-width wrap pulls in at least 2 columns.
        s->detect_count = 0;
        return;
    }
    // v8.6: the autodetect heuristic proved unreliable in practice. edit.com's
    // own status line ("[CRLF] [UTF-8] [空格:4] 1:1" + its trailing padding)
    // looks exactly like a "padded row with packed-in content", so the model
    // was wrongly shrunk from 120 to 103, wrecking the whole layout - far
    // worse than the ~1-column offset it was trying to fix. Only shrink when
    // the mismatch is unambiguous: the packed-in run must start at least 5
    // columns before the model's right edge (a genuine multi-column overflow),
    // and the computed width must be a plausible terminal width (>= 40).
    if (mix_start >= s->cols - 5 || mix_start < 40) { s->detect_count = 0; return; }
    if (mix_start >= 8 && mix_start < s->cols) {
        screen_resize(s, mix_start, s->rows);   // shrink model to ConPTY width
        s->wraparound_pending = 1;              // screen_resize cleared it; the char that just
                                                // filled the row must still wrap to the next row
        s->detect_count = 100;                  // cool down for the rest of this frame
    } else {
        s->detect_count++;
        if (s->detect_count >= 5) s->detect_count = 100;   // runaway guard
    }
#endif
}

// v8.7: write a char + its current colors into the cell at (row,col), including
// the per-cell truecolor RGB565 arrays (or clear them to INVALID if the current
// color is not truecolor).
static void screen_write_cell(ScreenBuffer *s, int row, int col, WCHAR ch, WORD attr) {
    CHAR_INFO *cell = screen_cell(s, row, col);
    if (!cell) return;
    cell->Char.UnicodeChar = ch;
    cell->Attributes = attr;
    // v8.8: store RGB565 + a VALID flag (bit0=fg, bit1=bg). No sentinel value.
    unsigned char v = (s->fg_rgb_on ? 1 : 0) | (s->bg_rgb_on ? 2 : 0);
    if (s->in_alt_screen) {
        if (s->alt_fg_rgb) {
            s->alt_fg_rgb[row * s->cols + col] = s->fg_rgb_on ? rgb565(s->fg_r, s->fg_g, s->fg_b) : RGB565_WHITE;
            s->alt_bg_rgb[row * s->cols + col] = s->bg_rgb_on ? rgb565(s->bg_r, s->bg_g, s->bg_b) : RGB565_BLACK;
            s->alt_rgb_valid[row * s->cols + col] = v;
        }
    } else {
        int abs_row = s->scroll_top + row;
        if (abs_row >= 0 && abs_row < s->total_lines && s->fg_rgb) {
            s->fg_rgb[abs_row * s->cols + col] = s->fg_rgb_on ? rgb565(s->fg_r, s->fg_g, s->fg_b) : RGB565_WHITE;
            s->bg_rgb[abs_row * s->cols + col] = s->bg_rgb_on ? rgb565(s->bg_r, s->bg_g, s->bg_b) : RGB565_BLACK;
            s->rgb_valid[abs_row * s->cols + col] = v;
        }
    }
}

static void screen_put_cp(ScreenBuffer *s, unsigned int cp) {
    if (s->wraparound_pending) {
        s->cursor_x = 0;
        screen_newline(s);
        s->wraparound_pending = 0;
    }
    int wide = is_wide_cp(cp);
    WORD attr = build_attr(s);
    if (cp >= 0x10000) {
        WCHAR high = (WCHAR)(0xD800 + ((cp - 0x10000) >> 10));
        WCHAR low = (WCHAR)(0xDC00 + ((cp - 0x10000) & 0x3FF));
        screen_write_cell(s, s->cursor_y, s->cursor_x, high, attr);
        if (s->cursor_x + 1 < s->cols) {
            screen_write_cell(s, s->cursor_y, s->cursor_x + 1, low, attr);
        }
    } else {
        screen_write_cell(s, s->cursor_y, s->cursor_x, (WCHAR)cp, attr);
        if (wide) {
            screen_write_cell(s, s->cursor_y, s->cursor_x + 1, 0, attr);
        }
    }
    if (s->cursor_x + (wide ? 2 : 1) < s->cols) {
        s->cursor_x += (wide ? 2 : 1);
    } else if (s->auto_wrap) {
        s->wraparound_pending = 1;
        if (s->detect_count <= 100) detect_conpty_width(s);
    }
}

static void screen_send_response(ScreenBuffer *s, const char *resp) {
    int len = (int)strlen(resp);
    if (len < (int)sizeof(s->response_buf)) {
        memcpy(s->response_buf, resp, len);
        s->response_len = len;
    }
}

// ============================================================
// Parse parameters
// ============================================================
static int parse_params(const char *buf, int len, int *params, int max) {
    int count = 0, val = 0, has = 0;
    for (int i = 0; i < len && count < max; i++) {
        char c = buf[i];
        if (c >= '0' && c <= '9') {
            val = val * 10 + (c - '0');
            if (val > 9999999) val = 9999999;   // v7: clamp -> no int overflow
            has = 1;
        }
        else if (c == ';' || c == ':') { params[count++] = has ? val : 0; val = 0; has = 0; }
    }
    if ((has || count > 0) && count < max) params[count++] = has ? val : 0;
    return count;
}

// ============================================================
// Process SGR (colors/attributes)
// ============================================================
static void process_sgr(ScreenBuffer *s, int *p, int n) {
    if (n == 0) { s->fg_color = 7; s->bg_color = 0; s->bold = s->underline = s->reverse_video = 0; s->fg_rgb_on = s->bg_rgb_on = 0; s->current_attr = build_attr(s); return; }
    for (int i = 0; i < n; i++) {
        int v = p[i];
        switch (v) {
            case 0: s->fg_color = 7; s->bg_color = 0; s->bold = s->underline = s->reverse_video = 0; s->fg_rgb_on = s->bg_rgb_on = 0; break;
            case 1: s->bold = 1; break;
            case 4: s->underline = 1; break;
            case 7: s->reverse_video = 1; break;
            case 22: s->bold = 0; break;
            case 24: s->underline = 0; break;
            case 27: s->reverse_video = 0; break;
            case 39: s->fg_color = 7; s->fg_rgb_on = 0; break;
            case 49: s->bg_color = 0; s->bg_rgb_on = 0; break;
            default:
                if (v >= 30 && v <= 37) { s->fg_color = v - 30; s->fg_rgb_on = 0; }
                else if (v >= 40 && v <= 47) { s->bg_color = v - 40; s->bg_rgb_on = 0; }
                else if (v >= 90 && v <= 97) { s->fg_color = v - 90 + 8; s->fg_rgb_on = 0; }
                else if (v >= 100 && v <= 107) { s->bg_color = v - 100 + 8; s->bg_rgb_on = 0; }
                else if (v == 38 && i + 2 < n && p[i+1] == 5) {
                    int c = p[i+2];
                    if (c < 16) s->fg_color = c;
                    else if (c < 232) { c -= 16; s->fg_color = ((c/36)>2?1:0)|((c/6%6)>2?2:0)|((c%6)>2?4:0); if((c/36)>3||(c/6%6)>3||(c%6)>3) s->fg_color|=8; }
                    else s->fg_color = (c-232)>12?15:7;
                    s->fg_rgb_on = 0;
                    i += 2;
                } else if (v == 48 && i + 2 < n && p[i+1] == 5) {
                    int c = p[i+2];
                    if (c < 16) s->bg_color = c;
                    else if (c < 232) { c -= 16; s->bg_color = ((c/36)>2?1:0)|((c/6%6)>2?2:0)|((c%6)>2?4:0); }
                    else s->bg_color = (c-232)>12?7:0;
                    s->bg_rgb_on = 0;
                    i += 2;
                } else if (v == 38 && i + 4 < n && p[i+1] == 2) {
                    // v8.7: keep truecolor (was crushed to 16-color -> dark colors
                    // like edit.com's blue menu bar became black).
                    int r = p[i+2], g = p[i+3], b = p[i+4];
                    s->fg_color = (r>127?4:0)|(g>127?2:0)|(b>127?1:0); if(r>191||g>191||b>191) s->fg_color|=8;
                    s->fg_r = r; s->fg_g = g; s->fg_b = b; s->fg_rgb_on = 1;
                    i += 4;
                } else if (v == 48 && i + 4 < n && p[i+1] == 2) {
                    int r = p[i+2], g = p[i+3], b = p[i+4];
                    s->bg_color = (r>127?4:0)|(g>127?2:0)|(b>127?1:0);
                    s->bg_r = r; s->bg_g = g; s->bg_b = b; s->bg_rgb_on = 1;
                    i += 4;
                }
                break;
        }
    }
    s->current_attr = build_attr(s);
}

// ============================================================
// Execute CSI sequence
// ============================================================
static void execute_csi(ScreenBuffer *s, char final, char prefix, const char *params_str, int params_len, const char *inter, int inter_len) {
    (void)inter;
    int params[32] = {0};
    int pc = parse_params(params_str, params_len, params, 32);
    int p1 = pc > 0 ? params[0] : 0;
    int p2 = pc > 1 ? params[1] : 0;

    // Ignore sequences with intermediate bytes we don't handle
    if (inter_len > 0) return;

    if (prefix == '?') {
        // Private mode set/reset
        if (final == 'h') {
            for (int i = 0; i < pc; i++) {
                switch (params[i]) {
                    case 1: s->app_cursor_keys = 1; break;
                    case 7: s->auto_wrap = 1; break;
                    case 25: s->cursor_visible = 1; break;
                    case 47: case 1047:
                        if (!s->in_alt_screen) {
                            s->in_alt_screen = 1; s->alt_scroll_top = s->scroll_top;
                            s->alt_hist_lines = s->hist_lines;   // v8.16: preserve scrollback
                            for (int j = 0; j < s->rows * s->cols; j++) {
                                s->alt_buffer[j].Char.UnicodeChar = L' '; s->alt_buffer[j].Attributes = s->current_attr;
                                if (s->alt_fg_rgb) { s->alt_fg_rgb[j] = RGB565_WHITE; s->alt_bg_rgb[j] = RGB565_BLACK; s->alt_rgb_valid[j] = 0; }
                            }
                        }
                        break;
                    case 1049:
                        if (!s->in_alt_screen) {
                            s->saved_cx = s->cursor_x; s->saved_cy = s->cursor_y;
                            s->in_alt_screen = 1; s->alt_scroll_top = s->scroll_top;
                            s->alt_hist_lines = s->hist_lines;   // v8.16: preserve scrollback
                            for (int j = 0; j < s->rows * s->cols; j++) {
                                s->alt_buffer[j].Char.UnicodeChar = L' '; s->alt_buffer[j].Attributes = s->current_attr;
                                if (s->alt_fg_rgb) { s->alt_fg_rgb[j] = RGB565_WHITE; s->alt_bg_rgb[j] = RGB565_BLACK; s->alt_rgb_valid[j] = 0; }
                            }
                            s->cursor_x = s->cursor_y = 0;
                        }
                        break;
                    case 1048: s->saved_cx = s->cursor_x; s->saved_cy = s->cursor_y; break;   // v8: save cursor
                    case 1000: case 1002: case 1003: s->mouse_tracking = params[i]; break;
                    case 1006: s->mouse_sgr = 1; break;
                    case 2004: s->bracketed_paste = 1; break;
                    case 9001: s->win32_input_mode = 1; break;
                    case 6: s->origin_mode = 1; break;
                }
            }
        } else if (final == 'l') {
            for (int i = 0; i < pc; i++) {
                switch (params[i]) {
                    case 1: s->app_cursor_keys = 0; break;
                    case 7: s->auto_wrap = 0; break;
                    case 25: s->cursor_visible = 0; break;
                    case 47: case 1047:
                        if (s->in_alt_screen) {
                            s->in_alt_screen = 0; s->scroll_top = s->alt_scroll_top;
                            s->hist_lines = s->alt_hist_lines;   // v8.16: restore scrollback
                            // clamp any stale scroll offset to the restored depth
                            { int pi2 = s->pane_index; if (pi2 >= 0 && pi2 < MAX_PANES && g_mux.panes[pi2].scroll_offset > s->hist_lines) g_mux.panes[pi2].scroll_offset = s->hist_lines; }
                        }
                        break;
                    case 1049:
                        if (s->in_alt_screen) {
                            s->in_alt_screen = 0; s->scroll_top = s->alt_scroll_top;
                            s->hist_lines = s->alt_hist_lines;   // v8.16: restore scrollback
                            s->cursor_x = s->saved_cx; s->cursor_y = s->saved_cy;
                            { int pi2 = s->pane_index; if (pi2 >= 0 && pi2 < MAX_PANES && g_mux.panes[pi2].scroll_offset > s->hist_lines) g_mux.panes[pi2].scroll_offset = s->hist_lines; }
                        }
                        break;
                    case 1048: s->cursor_x = s->saved_cx; s->cursor_y = s->saved_cy; s->wraparound_pending = 0; break;   // v8: restore cursor
                    case 1000: case 1002: case 1003: s->mouse_tracking = 0; break;
                    case 1006: s->mouse_sgr = 0; break;
                    case 2004: s->bracketed_paste = 0; break;
                    case 9001: s->win32_input_mode = 0; break;
                    case 6: s->origin_mode = 0; break;
                }
            }
        }
        return;
    }

    if (prefix == '>' || prefix == '=' || prefix == '<' || prefix == '!') return;

    switch (final) {
        case 'A': { int n = p1 ? p1 : 1; s->cursor_y -= n; if (s->cursor_y < 0) s->cursor_y = 0; s->wraparound_pending = 0; break; }
        case 'B': case 'e': { int n = p1 ? p1 : 1; s->cursor_y += n; if (s->cursor_y >= s->rows) s->cursor_y = s->rows - 1; s->wraparound_pending = 0; break; }
        case 'C': case 'a': { int n = p1 ? p1 : 1; s->cursor_x += n; if (s->cursor_x >= s->cols) s->cursor_x = s->cols - 1; s->wraparound_pending = 0; break; }
        case 'D': { int n = p1 ? p1 : 1; s->cursor_x -= n; if (s->cursor_x < 0) s->cursor_x = 0; s->wraparound_pending = 0; break; }
        case 'E': { int n = p1 ? p1 : 1; s->cursor_x = 0; s->cursor_y += n; if (s->cursor_y >= s->rows) s->cursor_y = s->rows - 1; s->wraparound_pending = 0; break; }
        case 'F': { int n = p1 ? p1 : 1; s->cursor_x = 0; s->cursor_y -= n; if (s->cursor_y < 0) s->cursor_y = 0; s->wraparound_pending = 0; break; }
        case 'G': case '`': { s->cursor_x = (p1 ? p1 : 1) - 1; if (s->cursor_x >= s->cols) s->cursor_x = s->cols - 1; if (s->cursor_x < 0) s->cursor_x = 0; s->wraparound_pending = 0; break; }
        case 'H': case 'f': {
            s->cursor_y = (p1 ? p1 : 1) - 1; s->cursor_x = (p2 ? p2 : 1) - 1;
            if (s->origin_mode) s->cursor_y += s->scroll_region_top;
            if (s->cursor_y >= s->rows) s->cursor_y = s->rows - 1;
            if (s->cursor_y < 0) s->cursor_y = 0;
            if (s->cursor_x >= s->cols) s->cursor_x = s->cols - 1;
            if (s->cursor_x < 0) s->cursor_x = 0;
            s->wraparound_pending = 0;
            // v8.3: frame start - allow re-adapt each frame.
            // "CSI H" (no params) means 1;1 just like "CSI 1;1H" - p1/p2 are 0
            // when the sequence has no parameters, so compare with <= 1.
            if (p1 <= 1 && p2 <= 1) s->detect_count = 0;
            break;
        }
        case 'J': {
            WORD attr = build_attr(s);
            if (p1 == 0 || p1 == 2) {
                // 0J: clear from cursor down; 2J: clear whole display
                int sy = (p1 == 0) ? s->cursor_y : 0, sx = (p1 == 0) ? s->cursor_x : 0;
                for (int y = sy; y < s->rows; y++)
                    for (int x = (y == sy ? sx : 0); x < s->cols; x++)
                        screen_write_cell(s, y, x, L' ', attr);
            }
            if (p1 == 1) {
                // 1J: clear from top to cursor
                for (int y = 0; y <= s->cursor_y; y++) {
                    int ex = (y == s->cursor_y) ? s->cursor_x : s->cols - 1;
                    for (int x = 0; x <= ex; x++) screen_write_cell(s, y, x, L' ', attr);
                }
            }
            // v8.15: Windows "cls" emits "CSI 2J" and/or "CSI 3J" and clears the
            // scrollback - the user must NOT be able to scroll up into pre-cls
            // content afterwards. 2J = erase display (+ Windows: scrollback),
            // 3J = erase scrollback only. Clearing hist_lines (and any active
            // scroll offset) makes do_scroll refuse to scroll, matching the
            // behavior of a real Windows console.
            // v8.16: only when NOT in the alternate screen. Full-screen apps
            // (nano/vim) clear the alt screen with 2J on startup - that must
            // not wipe the main screen's scrollback.
            if ((p1 == 2 || p1 == 3) && !s->in_alt_screen) {
                s->hist_lines = 0;
                int pi = s->pane_index;
                if (pi >= 0 && pi < MAX_PANES) g_mux.panes[pi].scroll_offset = 0;
            }
            break;
        }
        case 'K': {
            WORD attr = build_attr(s);
            int sx = (p1 == 1 || p1 == 2) ? 0 : s->cursor_x;
            int ex = (p1 == 0 || p1 == 2) ? s->cols - 1 : s->cursor_x;
            for (int x = sx; x <= ex; x++) screen_write_cell(s, s->cursor_y, x, L' ', attr);
            break;
        }
        case 'L': screen_scroll_down(s, s->cursor_y, s->scroll_region_bottom, p1 ? p1 : 1); break;
        case 'M': screen_scroll_up(s, s->cursor_y, s->scroll_region_bottom, p1 ? p1 : 1); break;
        case 'P': {
            int n = p1 ? p1 : 1;
            for (int x = s->cursor_x; x < s->cols; x++) {
                CHAR_INFO *d = screen_cell(s, s->cursor_y, x), *sr = screen_cell(s, s->cursor_y, x + n);
                if (d) { if (sr) *d = *sr; else screen_write_cell(s, s->cursor_y, x, L' ', build_attr(s)); }
            }
            break;
        }
        case '@': {
            int n = p1 ? p1 : 1; WORD attr = build_attr(s);
            for (int x = s->cols - 1; x >= s->cursor_x + n; x--) { CHAR_INFO *d = screen_cell(s, s->cursor_y, x), *sr = screen_cell(s, s->cursor_y, x - n); if (d && sr) *d = *sr; }
            for (int x = s->cursor_x; x < s->cursor_x + n && x < s->cols; x++) screen_write_cell(s, s->cursor_y, x, L' ', attr);
            break;
        }
        case 'X': {
            int n = p1 ? p1 : 1; WORD attr = build_attr(s);
            for (int x = s->cursor_x; x < s->cursor_x + n && x < s->cols; x++) screen_write_cell(s, s->cursor_y, x, L' ', attr);
            break;
        }
        case 'S': screen_scroll_up(s, s->scroll_region_top, s->scroll_region_bottom, p1 ? p1 : 1); break;
        case 'T': screen_scroll_down(s, s->scroll_region_top, s->scroll_region_bottom, p1 ? p1 : 1); break;
        case 'd': { s->cursor_y = (p1 ? p1 : 1) - 1; if (s->cursor_y >= s->rows) s->cursor_y = s->rows - 1; if (s->cursor_y < 0) s->cursor_y = 0; s->wraparound_pending = 0; break; }
        case 'm': process_sgr(s, params, pc); break;
        case 'r': {
            int top = p1 ? p1 : 1, bot = p2 ? p2 : s->rows;
            s->scroll_region_top = top - 1; s->scroll_region_bottom = bot - 1;
            if (s->scroll_region_top < 0) s->scroll_region_top = 0;
            if (s->scroll_region_bottom >= s->rows) s->scroll_region_bottom = s->rows - 1;
            if (s->scroll_region_top >= s->scroll_region_bottom) { s->scroll_region_top = 0; s->scroll_region_bottom = s->rows - 1; }
            s->cursor_x = 0; s->cursor_y = s->origin_mode ? s->scroll_region_top : 0;
            s->wraparound_pending = 0; break;
        }
        case 's': s->saved_cx = s->cursor_x; s->saved_cy = s->cursor_y; break;
        case 'u': s->cursor_x = s->saved_cx; s->cursor_y = s->saved_cy; s->wraparound_pending = 0; break;
        case 'n': if (p1 == 5) screen_send_response(s, "\x1b[0n"); else if (p1 == 6) { char r[32]; snprintf(r, sizeof(r), "\x1b[%d;%dR", s->cursor_y + 1, s->cursor_x + 1); screen_send_response(s, r); } break;
        case 'c': screen_send_response(s, "\x1b[?62;c"); break;
        case 't':
            if (p1 == 18) { char r[32]; snprintf(r, sizeof(r), "\x1b[8;%d;%dt", s->rows, s->cols); screen_send_response(s, r); }
            else if (p1 == 8 && pc >= 3 && p2 > 0 && params[2] > 0) {
                // v8: "CSI 8 ; rows ; cols t" - the application resized the
                // console buffer (full-screen/legacy apps such as edit, or
                // `mode con:cols=.. lines=..`). ConPTY reports the new size
                // here; if we ignore it our screen model keeps the old size and
                // the app's layout looks misaligned. Follow the new size.
                int nr = p2, nc = params[2];
                if (nr >= 2 && nr <= 500 && nc >= 2 && nc <= 1000)
                    screen_resize(s, nc, nr);
            }
            break;
        case 'g': if (p1 == 0 && s->cursor_x < 512) s->tab_stops[s->cursor_x] = 0; else if (p1 == 3) memset(s->tab_stops, 0, sizeof(s->tab_stops)); break;
    }
}

// ============================================================
// Execute OSC sequence
// ============================================================
static inline int ci_str_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + ('a' - 'A')) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + ('a' - 'A')) : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return (*a == 0 && *b == 0);
}

static inline int ci_str_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        char ca = (*str >= 'A' && *str <= 'Z') ? (char)(*str + ('a' - 'A')) : *str;
        char cb = (*prefix >= 'A' && *prefix <= 'Z') ? (char)(*prefix + ('a' - 'A')) : *prefix;
        if (ca != cb) return 0;
        str++; prefix++;
    }
    return 1;
}

static void sanitize_title(const char *raw, int raw_len, char *out, int out_size) {
    if (!raw || raw_len <= 0 || out_size <= 0) {
        if (out && out_size > 0) out[0] = 0;
        return;
    }
    char buf[512];
    int len = raw_len < 511 ? raw_len : 511;
    memcpy(buf, raw, len);
    buf[len] = 0;

    // Trim trailing whitespace and control chars (e.g. BEL 0x07)
    while (len > 0 && ((unsigned char)buf[len - 1] <= ' ' || buf[len - 1] == 0x07)) {
        buf[--len] = 0;
    }

    const char *p = buf;

    // 1. Strip Windows "管理员: " / "Administrator: " prefixes
    if (strncmp(p, "\xe7\xae\xa1\xe7\x90\x86\xe5\x91\x98", 9) == 0) { // "管理员" in UTF-8
        p += 9;
        while (*p == ':' || *p == ' ') p++;
    } else if (ci_str_starts_with(p, "Administrator")) {
        p += 13;
        while (*p == ':' || *p == ' ') p++;
    }

    // 2. If conhost/cmd prefixed the title with ":   " (e.g. "cmd.exe:   T" or ":   T")
    const char *colon = strstr(p, ":   ");
    if (!colon) colon = strstr(p, ":  ");
    if (!colon) colon = strstr(p, ": ");
    if (colon) {
        p = colon + 1;
        while (*p == ' ' || *p == ':') p++;
    }

    // 3. If there is a " - " separator after an exe path (e.g. "cmd.exe - T")
    const char *dash = strstr(p, " - ");
    if (dash && (strstr(buf, ".exe") || strstr(buf, "\\") || strstr(buf, "/"))) {
        p = dash + 3;
        while (*p == ' ') p++;
    }

    // 4. Strip any stray leading colons, dashes or whitespace
    while (*p == ':' || *p == '-' || *p == ' ') p++;

    // 5. If string is still a file path e.g. "C:\Windows\System32\cmd.exe"
    if (strstr(p, "\\") || strstr(p, "/")) {
        const char *last_slash = p;
        for (const char *s = p; *s; s++) {
            if (*s == '\\' || *s == '/') last_slash = s + 1;
        }
        p = last_slash;
    }

    // Normalize common shell names
    if (ci_str_eq(p, "cmd.exe") || ci_str_eq(p, "cmd")) {
        p = "cmd";
    } else if (ci_str_eq(p, "powershell.exe") || ci_str_eq(p, "powershell")) {
        p = "PowerShell";
    }

    if (!*p) p = "cmd";

    strncpy(out, p, out_size - 1);
    out[out_size - 1] = 0;
}

static void execute_osc(ScreenBuffer *s) {
    // Only handle OSC 0, 1, 2 (window title)
    if ((s->osc_num == 0 || s->osc_num == 1 || s->osc_num == 2) && s->osc_len > 0) {
        int idx = s->pane_index;
        if (idx >= 0 && idx < g_mux.pane_count && g_mux.panes[idx].active) {
            sanitize_title(s->osc_buf, s->osc_len, g_mux.panes[idx].title, sizeof(g_mux.panes[idx].title));
        }
    }
    // All other OSC sequences are silently ignored
    s->osc_num = -1; s->osc_len = 0; s->osc_sep = 0;   // v8.9: always reset
}

// ============================================================
// Execute ESC sequence
// ============================================================
static void execute_esc(ScreenBuffer *s, char final, const char *inter, int inter_len) {
    (void)inter;
    if (inter_len > 0) return; // Ignore sequences with intermediate bytes

    switch (final) {
        case 'D': screen_newline(s); break;
        case 'E': s->cursor_x = 0; screen_newline(s); break;
        case 'M': if (s->cursor_y <= s->scroll_region_top) screen_scroll_down(s, s->scroll_region_top, s->scroll_region_bottom, 1); else s->cursor_y--; break;
        case '7': s->saved_cx = s->cursor_x; s->saved_cy = s->cursor_y; break;
        case '8': s->cursor_x = s->saved_cx; s->cursor_y = s->saved_cy; s->wraparound_pending = 0; break;
        case '=': s->app_keypad = 1; break;
        case '>': s->app_keypad = 0; break;
        case 'c': s->fg_color = 7; s->bg_color = 0; s->bold = s->underline = s->reverse_video = 0; s->fg_rgb_on = s->bg_rgb_on = 0; s->current_attr = build_attr(s); break;
        case 'H': if (s->cursor_x < 512) s->tab_stops[s->cursor_x] = 1; break;
    }
}

// ============================================================
// Character classification for VT parser
// ============================================================
static inline int is_param_byte(unsigned char c) { return c >= 0x30 && c <= 0x3F; } // 0-9:;<=>?
static inline int is_inter_byte(unsigned char c) { return c >= 0x20 && c <= 0x2F; } // SP-/
static inline int is_final_byte(unsigned char c) { return c >= 0x40 && c <= 0x7E; } // @-~
static inline int is_c0(unsigned char c) { return c < 0x20 || c == 0x7F; }
static inline int is_printable(unsigned char c) { return c >= 0x20 && c < 0x7F; }

// ============================================================
// VT parser state machine
// ============================================================
static void screen_process_byte(ScreenBuffer *s, unsigned char c) {
    // Handle special characters that work in any state
    if (c == 0x18 || c == 0x1A) { s->state = ST_NORMAL; return; } // CAN, SUB
    if (c == 0x1B) {
        // ESC always starts new sequence
        s->state = ST_ESC;
        s->param_len = 0;
        s->inter_len = 0;
        return;
    }

    switch (s->state) {
        case ST_NORMAL:
            if (c < 0x20) {
                // C0 controls
                switch (c) {
                    case 0x07: break; // BEL
                    case 0x08: if (s->cursor_x > 0) s->cursor_x--; s->wraparound_pending = 0; break; // BS
                    case 0x09: { int x = s->cursor_x + 1; while (x < s->cols && x < 512 && !s->tab_stops[x]) x++; s->cursor_x = (x < s->cols) ? x : s->cols - 1; s->wraparound_pending = 0; } break; // HT
                    case 0x0A: case 0x0B: case 0x0C: screen_newline(s); s->wraparound_pending = 0; break; // LF, VT, FF
                    case 0x0D: s->cursor_x = 0; s->wraparound_pending = 0; break; // CR
                    case 0x0E: break; // SO - ignore
                    case 0x0F: break; // SI - ignore
                }
            } else if (c == 0x7F) {
                // DEL - ignore
            } else if (c >= 0x80 && c <= 0x9F) {
                // C1 controls (8-bit)
                switch (c) {
                    case 0x84: screen_newline(s); break; // IND
                    case 0x85: s->cursor_x = 0; screen_newline(s); break; // NEL
                    case 0x88: if (s->cursor_x < 512) s->tab_stops[s->cursor_x] = 1; break; // HTS
                    case 0x8D: if (s->cursor_y <= s->scroll_region_top) screen_scroll_down(s, s->scroll_region_top, s->scroll_region_bottom, 1); else s->cursor_y--; break; // RI
                    case 0x90: s->state = ST_DCS_ENTRY; s->param_len = 0; s->inter_len = 0; break; // DCS
                    case 0x98: case 0x9E: case 0x9F: s->state = ST_SOS_STRING; break; // SOS, PM, APC
                    case 0x9B: s->state = ST_CSI_ENTRY; s->param_len = 0; s->inter_len = 0; break; // CSI
                    case 0x9D: s->state = ST_OSC_STRING; s->osc_num = -1; s->osc_len = 0; s->osc_sep = 0; break; // OSC
                    case 0x9C: break; // ST - ignore
                }
            } else {
                screen_put_cp(s, (unsigned char)c);
            }
            break;

        case ST_ESC:
            if (is_inter_byte(c)) {
                if (s->inter_len < 15) s->inter_buf[s->inter_len++] = c;
                s->state = ST_ESC_INTER;
            } else if (c >= 0x30 && c <= 0x7E) {
                if (c == '[') { s->state = ST_CSI_ENTRY; s->param_len = 0; s->inter_len = 0; }
                else if (c == ']') { s->state = ST_OSC_STRING; s->osc_num = -1; s->osc_len = 0; s->osc_sep = 0; }
                else if (c == 'P') { s->state = ST_DCS_ENTRY; s->param_len = 0; s->inter_len = 0; }
                else if (c == 'X' || c == '^' || c == '_') { s->state = ST_SOS_STRING; }
                else { execute_esc(s, c, s->inter_buf, s->inter_len); s->state = ST_NORMAL; }
            } else if (is_c0(c)) {
                // v7 fix: return to ground state FIRST, then execute the C0.
                // (Previously this recursed while still in ST_ESC -> infinite
                // recursion / stack overflow on "ESC <ctrl byte>".)
                s->state = ST_NORMAL;
                screen_process_byte(s, c);
            } else {
                s->state = ST_NORMAL;
            }
            break;

        case ST_ESC_INTER:
            if (is_inter_byte(c)) {
                if (s->inter_len < 15) s->inter_buf[s->inter_len++] = c;
            } else if (c >= 0x30 && c <= 0x7E) {
                execute_esc(s, c, s->inter_buf, s->inter_len);
                s->state = ST_NORMAL;
            } else {
                s->state = ST_NORMAL;
            }
            break;

        case ST_CSI_ENTRY:
            if (is_param_byte(c)) {
                if (s->param_len < 255) s->param_buf[s->param_len++] = c;
                s->state = ST_CSI_PARAM;
            } else if (is_inter_byte(c)) {
                if (s->inter_len < 15) s->inter_buf[s->inter_len++] = c;
                s->state = ST_CSI_INTER;
            } else if (is_final_byte(c)) {
                execute_csi(s, c, 0, s->param_buf, s->param_len, s->inter_buf, s->inter_len);
                s->state = ST_NORMAL;
            } else if (is_c0(c)) {
                // Ignore C0 in CSI
            } else {
                s->state = ST_CSI_IGNORE;
            }
            break;

        case ST_CSI_PARAM:
            if (is_param_byte(c)) {
                if (s->param_len < 255) s->param_buf[s->param_len++] = c;
            } else if (is_inter_byte(c)) {
                if (s->inter_len < 15) s->inter_buf[s->inter_len++] = c;
                s->state = ST_CSI_INTER;
            } else if (is_final_byte(c)) {
                char prefix = 0;
                if (s->param_len > 0 && (s->param_buf[0] == '?' || s->param_buf[0] == '>' || s->param_buf[0] == '=' || s->param_buf[0] == '<' || s->param_buf[0] == '!')) {
                    prefix = s->param_buf[0];
                }
                execute_csi(s, c, prefix, s->param_buf, s->param_len, s->inter_buf, s->inter_len);
                s->state = ST_NORMAL;
            } else if (is_c0(c)) {
                // Ignore
            } else {
                s->state = ST_CSI_IGNORE;
            }
            break;

        case ST_CSI_INTER:
            if (is_inter_byte(c)) {
                if (s->inter_len < 15) s->inter_buf[s->inter_len++] = c;
            } else if (is_final_byte(c)) {
                char prefix = 0;
                if (s->param_len > 0 && (s->param_buf[0] == '?' || s->param_buf[0] == '>' || s->param_buf[0] == '=')) prefix = s->param_buf[0];
                execute_csi(s, c, prefix, s->param_buf, s->param_len, s->inter_buf, s->inter_len);
                s->state = ST_NORMAL;
            } else {
                s->state = ST_CSI_IGNORE;
            }
            break;

        case ST_CSI_IGNORE:
            if (is_final_byte(c)) s->state = ST_NORMAL;
            break;

        case ST_OSC_STRING:
            if (c == 0x07 || c == 0x9C) {
                // BEL or ST terminates OSC
                execute_osc(s);
                s->state = ST_NORMAL;
            } else if (c == 0x1B) {
                // Will check for ST (ESC \) in main ESC handler
                // For now, just terminate
                execute_osc(s);
                s->state = ST_ESC;
                s->param_len = 0;
                s->inter_len = 0;
            } else if (c >= 0x20 && c < 0x7F) {
                // v8.9: fixed OSC parsing. The ';' separator must be consumed
                // exactly once (osc_sep); only characters AFTER it are title
                // text. The old code treated ';' as title text once osc_num
                // reached 0, so every title got a leading ';' (and because
                // osc_buf was never NUL-terminated, strrchr in execute_osc
                // read stale bytes from previous OSCs -> "cmd.exe;" garbage).
                if (!s->osc_sep) {
                    if (c >= '0' && c <= '9') {
                        s->osc_num = (s->osc_num < 0 ? 0 : s->osc_num) * 10 + (c - '0');
                    } else if (c == ';') {
                        s->osc_sep = 1;
                    } else {
                        s->osc_sep = 1;   // tolerate odd prefixes (e.g. '?')
                    }
                } else {
                    if (s->osc_len < 511) s->osc_buf[s->osc_len++] = c;
                }
            }
            break;

        case ST_DCS_ENTRY:
        case ST_DCS_PARAM:
        case ST_DCS_INTER:
        case ST_DCS_PASSTHROUGH:
        case ST_DCS_IGNORE:
            // Consume everything until ST
            if (c == 0x9C) s->state = ST_NORMAL;
            else if (c == 0x1B) { s->state = ST_ESC; s->param_len = 0; s->inter_len = 0; }
            break;

        case ST_SOS_STRING:
            // Consume everything until ST
            if (c == 0x9C) s->state = ST_NORMAL;
            else if (c == 0x1B) { s->state = ST_ESC; s->param_len = 0; s->inter_len = 0; }
            break;
    }
}

static void screen_process_output(ScreenBuffer *s, const char *data, int len) {
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];

        // UTF-8 handling only in normal state
        if (s->state == ST_NORMAL) {
            if (c >= 0xC0 && c < 0xFE) {
                if ((c & 0xE0) == 0xC0) {
                    if (c < 0xC2) continue;            // overlong 2-byte lead - drop
                    s->utf8_cp = c & 0x1F; s->utf8_state = 1; continue;
                }
                else if ((c & 0xF0) == 0xE0) { s->utf8_cp = c & 0x0F; s->utf8_state = 2; continue; }
                else if ((c & 0xF8) == 0xF0) { s->utf8_cp = c & 0x07; s->utf8_state = 3; continue; }
                else continue;                          // 0xF8..0xFD / 0xFE - invalid lead
            } else if (s->utf8_state == 0 && c >= 0xA0 && c < 0xC0) {
                continue;   // stray continuation byte with no sequence pending - drop
            }
            // (continuation bytes 0x80..0xBF while utf8_state > 0 are handled
            //  below; 0x80..0x9F with no pending sequence still reach the C1
            //  control handler for 8-bit CSI/OSC compatibility)
        }
        if (s->utf8_state > 0) {
            if ((c & 0xC0) == 0x80) {
                s->utf8_cp = (s->utf8_cp << 6) | (c & 0x3F);
                if (--s->utf8_state == 0) {
                    if (s->state == ST_NORMAL)
                        screen_put_cp(s, s->utf8_cp);
                }
                continue;
            } else {
                s->utf8_state = 0;   // incomplete sequence - restart with this byte
            }
        }

        screen_process_byte(s, c);
    }
}

// Returns 1 on success, 0 on allocation failure (old buffers kept intact).
static int screen_resize(ScreenBuffer *s, int nc, int nr) {
    if (nc < 1) nc = 1;
    if (nr < 1) nr = 1;
    if (nc == s->cols && nr == s->rows) return 1;
    int nt = nr + SCROLL_BUF_LINES;
    CHAR_INFO *nb = (CHAR_INFO *)calloc(nt * nc, sizeof(CHAR_INFO));
    CHAR_INFO *na = (CHAR_INFO *)calloc(nr * nc, sizeof(CHAR_INFO));
    // v8.8: resize the truecolor + valid arrays together with the buffers
    WORD *nfr = (WORD *)malloc(nt * nc * sizeof(WORD));
    WORD *nbr = (WORD *)malloc(nt * nc * sizeof(WORD));
    WORD *nafr = (WORD *)malloc(nr * nc * sizeof(WORD));
    WORD *nabr = (WORD *)malloc(nr * nc * sizeof(WORD));
    unsigned char *nrv = (unsigned char *)calloc(nt * nc, 1);
    unsigned char *nav = (unsigned char *)calloc(nr * nc, 1);
    if (!nb || !na || !nfr || !nbr || !nafr || !nabr || !nrv || !nav) {
        free(nb); free(na); free(nfr); free(nbr); free(nafr); free(nabr);
        free(nrv); free(nav);
        return 0;   // v7: keep old buffers on OOM
    }
    WORD def = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    for (int i = 0; i < nt * nc; i++) { nb[i].Char.UnicodeChar = L' '; nb[i].Attributes = def; nfr[i] = RGB565_WHITE; nbr[i] = RGB565_BLACK; }
    for (int i = 0; i < nr * nc; i++) { na[i].Char.UnicodeChar = L' '; na[i].Attributes = def; nafr[i] = RGB565_WHITE; nabr[i] = RGB565_BLACK; }
    int cr = s->rows < nr ? s->rows : nr, cc = s->cols < nc ? s->cols : nc;
    int nst = nt - nr;

    // 1. Copy scrollback history so resizing does not wipe previous output
    int old_hist = s->hist_lines;
    if (old_hist > SCROLL_BUF_LINES) old_hist = SCROLL_BUF_LINES;
    for (int h = 1; h <= old_hist; h++) {
        int old_r = s->scroll_top - h;
        int new_r = nst - h;
        if (old_r >= 0 && old_r < s->total_lines && new_r >= 0 && new_r < nt) {
            for (int x = 0; x < cc; x++) {
                int old_idx = old_r * s->cols + x;
                int new_idx = new_r * nc + x;
                nb[new_idx] = s->buffer[old_idx];
                if (s->fg_rgb) nfr[new_idx] = s->fg_rgb[old_idx];
                if (s->bg_rgb) nbr[new_idx] = s->bg_rgb[old_idx];
                if (s->rgb_valid) nrv[new_idx] = s->rgb_valid[old_idx];
            }
        }
    }

    // 2. Copy visible screen rows
    for (int y = 0; y < cr; y++) {
        int new_r = nst + y;
        for (int x = 0; x < cc; x++) {
            CHAR_INFO *src = screen_cell(s, y, x);
            if (src) nb[new_r * nc + x] = *src;
            // copy truecolor + valid for the visible region
            WORD fv = RGB565_WHITE, bv = RGB565_BLACK; unsigned char vv = 0;
            if (s->in_alt_screen) {
                if (s->alt_fg_rgb) {
                    fv = s->alt_fg_rgb[y * s->cols + x]; bv = s->alt_bg_rgb[y * s->cols + x];
                    vv = s->alt_rgb_valid ? s->alt_rgb_valid[y * s->cols + x] : 0;
                }
            } else {
                int abs_row = s->scroll_top + y;
                if (abs_row >= 0 && abs_row < s->total_lines && s->fg_rgb) {
                    fv = s->fg_rgb[abs_row * s->cols + x];
                    bv = s->bg_rgb[abs_row * s->cols + x];
                    vv = s->rgb_valid ? s->rgb_valid[abs_row * s->cols + x] : 0;
                }
            }
            nfr[new_r * nc + x] = fv;
            nbr[new_r * nc + x] = bv;
            nrv[new_r * nc + x] = vv;
        }
    }

    // 3. Copy alt buffer
    if (s->alt_buffer) {
        for (int y = 0; y < cr; y++) {
            for (int x = 0; x < cc; x++) {
                int old_idx = y * s->cols + x;
                int new_idx = y * nc + x;
                na[new_idx] = s->alt_buffer[old_idx];
                if (s->alt_fg_rgb) nafr[new_idx] = s->alt_fg_rgb[old_idx];
                if (s->alt_bg_rgb) nabr[new_idx] = s->alt_bg_rgb[old_idx];
                if (s->alt_rgb_valid) nav[new_idx] = s->alt_rgb_valid[old_idx];
            }
        }
    }

    free(s->buffer); free(s->alt_buffer);
    free(s->fg_rgb); free(s->bg_rgb); free(s->alt_fg_rgb); free(s->alt_bg_rgb);
    free(s->rgb_valid); free(s->alt_rgb_valid);
    s->buffer = nb; s->alt_buffer = na;
    s->fg_rgb = nfr; s->bg_rgb = nbr; s->alt_fg_rgb = nafr; s->alt_bg_rgb = nabr;
    s->rgb_valid = nrv; s->alt_rgb_valid = nav;
    s->cols = nc; s->rows = nr; s->total_lines = nt; s->scroll_top = nst;
    s->hist_lines = old_hist;
    if (s->alt_hist_lines > SCROLL_BUF_LINES) s->alt_hist_lines = SCROLL_BUF_LINES;
    if (s->cursor_x >= nc) s->cursor_x = nc - 1;
    if (s->cursor_y >= nr) s->cursor_y = nr - 1;
    s->scroll_region_top = 0; s->scroll_region_bottom = nr - 1;
    s->wraparound_pending = 0;                           // v7: stale pending-wrap would corrupt next line
    memset(s->tab_stops, 0, sizeof(s->tab_stops)); for (int i = 0; i < nc && i < 512; i += 8) s->tab_stops[i] = 1;
    return 1;
}

// ============================================================
// Rendering
// ============================================================
// v8.8: get the per-cell truecolor + valid flags. `ar` is the absolute
// buffer row when scrolling (vo > 0), -1 otherwise. Cells with no truecolor
// report fgv/bgv = 0 and fall back to the 16-color path.
static void cell_truecolor(ScreenBuffer *s, int row, int col, int ar, WORD *out_f, WORD *out_b, int *out_fv, int *out_bv) {
    *out_f = RGB565_WHITE; *out_b = RGB565_BLACK;
    *out_fv = *out_bv = 0;
    if (s->in_alt_screen) {
        if (row >= 0 && row < s->rows && s->alt_fg_rgb) {
            unsigned char v = s->alt_rgb_valid ? s->alt_rgb_valid[row * s->cols + col] : 0;
            *out_fv = (v & 1) ? 1 : 0;
            *out_bv = (v & 2) ? 1 : 0;
            *out_f = s->alt_fg_rgb[row * s->cols + col];
            *out_b = s->alt_bg_rgb[row * s->cols + col];
        }
        return;
    }
    if (ar >= 0 && ar < s->total_lines && s->fg_rgb) {
        unsigned char v = s->rgb_valid ? s->rgb_valid[ar * s->cols + col] : 0;
        *out_fv = (v & 1) ? 1 : 0;
        *out_bv = (v & 2) ? 1 : 0;
        *out_f = s->fg_rgb[ar * s->cols + col];
        *out_b = s->bg_rgb[ar * s->cols + col];
    }
}

// v8.18: draw the tab bar row. Layout: [termux][help][cmd×][cmd×]...[+]
// pane_idx specials: -2 = brand, -1 = [+], -3 = help tab.
// v8.25: modern GitHub-Dark inspired palette (truecolor) for the chrome UI:
//   tab bar bg        #161b22 (22,27,34)
//   inactive tab bg   #21262d (33,38,45)  fg #8b949e (139,148,158)
//   active tab bg     #1f6feb (31,111,235) fg white
//   brand bg          #8957e5 (137,87,229) hover #a371f7 (163,113,247)
//   close x           #f85149 (248,81,73)  hover red bg + white
//   plus              #3fb950 (63,185,80)  hover green bg + dark
#define TB_BG        "\x1b[48;2;22;27;34m"
#define TAB_IN_BG    "\x1b[48;2;33;38;45m"
#define TAB_IN_FG    "\x1b[38;2;139;148;158m"
#define TAB_ACT_BG   "\x1b[48;2;31;111;235m"
#define TAB_ACT_FG   "\x1b[38;2;255;255;255m"
#define BRAND_BG     "\x1b[48;2;137;87;229m"
#define BRAND_BG_HV  "\x1b[48;2;163;113;247m"
#define X_RED        "\x1b[38;2;248;81;73m"   // v8.49: keep the x red
#define X_RED_BG     "\x1b[48;2;248;81;73m"
#define PLUS_GREEN   "\x1b[38;2;63;185;80m"
#define PLUS_GREEN_BG "\x1b[48;2;63;185;80m"
#define DARK_FG      "\x1b[38;2;13;17;23m"
// v8.32: tab color palette (index 1..8, index 0 = default GitHub blue)
static const char *const TAB_COLOR_BG[9] = {
    "\x1b[48;2;31;111;235m",   // 0 default: blue
    "\x1b[48;2;31;111;235m",    // 1 blue (default highlight)
    "\x1b[48;2;63;185;80m",    // 2 green
    "\x1b[48;2;210;153;34m",   // 3 amber
    "\x1b[48;2;137;87;229m",   // 4 purple
    "\x1b[48;2;31;136;61m",    // 5 teal/green-dark
    "\x1b[48;2;121;192;255m",  // 6 light blue
    "\x1b[48;2;217;119;54m",   // 7 orange
    "\x1b[48;2;205;93;173m",   // 8 pink
};
// v8.36: DARK variants of the same palette for INACTIVE tabs (color always
// visible, but clearly dimmer than the active tab).
static const char *const TAB_COLOR_BG_DIM[9] = {
    "\x1b[48;2;22;62;128m",
    "\x1b[48;2;22;62;128m",   // 1 blue dim
    "\x1b[48;2;36;99;49m",
    "\x1b[48;2;110;82;30m",
    "\x1b[48;2;74;48;122m",
    "\x1b[48;2;24;80;48m",
    "\x1b[48;2;52;96;128m",
    "\x1b[48;2;112;66;34m",
    "\x1b[48;2;104;50;90m",
};

static void draw_tab_bar(char *out, int bs, int *posp) {
    int pos = *posp;
    g_mux.tab_count = 0; int col = 0;
    // brand "termux" - with leading AND trailing space (v8.39); user request.
    {
        const char *brand = " termux ";
        int blen = (int)strlen(brand);
        int bcols = utf8_cols(brand, blen);
        if (col + bcols + 4 <= g_mux.host_cols) {
            int bhover = (g_mouse_y == 0 &&   // v8.22: tab bar at top
                          g_mouse_x >= col && g_mouse_x < col + bcols);
            g_mux.tab_info[g_mux.tab_count].start_col = col;
            g_mux.tab_info[g_mux.tab_count].end_col = col + bcols;
            g_mux.tab_info[g_mux.tab_count].pane_idx = -2;
            if (bhover)
                pos += snprintf(out + pos, bs - pos, BRAND_BG_HV "\x1b[1m%s\x1b[22m", brand);
            else
                pos += snprintf(out + pos, bs - pos, BRAND_BG "%s", brand);
            col += bcols;
            g_mux.tab_count++;
        }
    }
    // v8.19: no persistent [help] tab - the termux brand is the only entry point
    // real pane tabs
    for (int i = 0; i < g_mux.pane_count; i++) {
        if (!g_mux.panes[i].active) continue;
        char nm[20]; trunc_utf8(nm, g_mux.panes[i].title[0] ? g_mux.panes[i].title : "cmd", sizeof(nm));
        char head[32];
        int hl = snprintf(head, sizeof(head), "[%s", nm);
        int hc = utf8_cols(head, hl);               // columns of "[title"
        int lc = hc + 1;                            // + 1 for the 'x' glyph
        if (col + lc + 4 + 4 > g_mux.host_cols) break;  // leave room for [+] and [*]
        // v8.38: no color dot anymore - hover target is the 'x' right after head
        int hovering = (g_mouse_y == 0 &&   // v8.22: tab bar at top
                        g_mouse_x >= col + hc && g_mouse_x < col + lc);
        g_mux.tab_info[g_mux.tab_count].start_col = col;
        g_mux.tab_info[g_mux.tab_count].pane_idx = i;
        int act = (i == g_mux.active_pane);
        // v8.36: EVERY tab (active or not) gets a 1-cell color dot + colored
        // background, so the tab width is constant and the color is always
        // visible. Active = bright bg + white bold text; inactive = dim bg +
        // gray text. No layout shift when the active tab changes.
        // v8.55: FIXED color indexing. The old `color & 7` silently folded
        // index 8 back to 0 (default blue), so choosing color #8 pink worked
        // internally but the tab stayed blue - "clicking 8 does nothing".
        // color is 0 (default) or 1..8; index the palette directly.
        int ci = g_mux.panes[i].color;
        if (ci < 0 || ci > 8) ci = 0;
        const char *actbg = TAB_COLOR_BG[ci];
        const char *dimbg = TAB_COLOR_BG_DIM[ci];
        // v8.38: no leading color-dot + space - the tab's colored background
        // IS the color indicator, and "[cmd" starts right at the tab's left
        // edge (no stray space before the bracket).
        if (act)
            pos += snprintf(out + pos, bs - pos, "%s" TAB_ACT_FG "\x1b[1m%s\x1b[22m", actbg, head);
        else
            pos += snprintf(out + pos, bs - pos, "%s\x1b[38;2;139;148;158m%s", dimbg, head);
        if (hovering)
            pos += snprintf(out + pos, bs - pos, X_RED_BG "\x1b[38;2;255;255;255m\xc3\x97");
        else
            pos += snprintf(out + pos, bs - pos, X_RED "\xc3\x97");
        // restore the tab's own colors before ']' (same bg as the tab body)
        if (act)
            pos += snprintf(out + pos, bs - pos, "%s" TAB_ACT_FG "]", actbg);
        else
            pos += snprintf(out + pos, bs - pos, "%s\x1b[38;2;139;148;158m]", dimbg);
        g_mux.tab_info[g_mux.tab_count].close_start = col + hc;
        g_mux.tab_info[g_mux.tab_count].close_end = col + hc + 1;
        col += lc + 1;
        g_mux.tab_info[g_mux.tab_count].end_col = col;
        g_mux.tab_count++;
    }
    // [+] button - leave a 1-column gap (tab bar bg) so an active/hovered last
    // tab's highlight never visually touches the '+' (v8.26)
    if (col < g_mux.host_cols - 4) { pos += snprintf(out + pos, bs - pos, TB_BG " "); col++; }
    if (col + 3 <= g_mux.host_cols - 4) {
        g_mux.tab_info[g_mux.tab_count].start_col = col;
        g_mux.tab_info[g_mux.tab_count].end_col = col + 3;
        g_mux.tab_info[g_mux.tab_count].pane_idx = -1;
        int phover = (g_mouse_y == 0 &&   // v8.22: tab bar at top
                      g_mouse_x >= col && g_mouse_x < col + 3);
        if (phover)
            pos += snprintf(out + pos, bs - pos, PLUS_GREEN_BG DARK_FG "[+]");
        else
            pos += snprintf(out + pos, bs - pos, PLUS_GREEN "[+]");
        col += 3;
        g_mux.tab_count++;
    }
    // Fill background spaces to the far right before [*] settings button
    pos += snprintf(out + pos, bs - pos, TB_BG);
    while (col < g_mux.host_cols - 3 && pos < bs - 8) { out[pos++] = ' '; col++; }

    // [*] Settings button anchored at far right (pane_idx == -3)
    if (col + 3 <= g_mux.host_cols) {
        g_mux.tab_info[g_mux.tab_count].start_col = col;
        g_mux.tab_info[g_mux.tab_count].end_col = col + 3;
        g_mux.tab_info[g_mux.tab_count].pane_idx = -3;
        int shover = (g_mouse_y == 0 && g_mouse_x >= col && g_mouse_x < col + 3);
        if (shover)
            pos += snprintf(out + pos, bs - pos, "\x1b[48;2;137;87;229m\x1b[38;2;255;255;255;1m[*]\x1b[0m");
        else
            pos += snprintf(out + pos, bs - pos, TAB_IN_BG "\x1b[38;2;139;148;158m[*]\x1b[0m");
        col += 3;
        g_mux.tab_count++;
    }
    pos += snprintf(out + pos, bs - pos, TB_BG);
    while (col < g_mux.host_cols && pos < bs - 4) { out[pos++] = ' '; col++; }
    pos += snprintf(out + pos, bs - pos, "\x1b[0m");
    *posp = pos;
}

static void chooser_geom(int host_rows, int host_cols, int *top, int *left, int *w, int *h) {
    (void)host_rows;
    int cw = 26;
    for (int i = 0; i < g_chooser_item_count; i++) {
        int nw = utf8_cols(g_chooser_items[i].name, (int)strlen(g_chooser_items[i].name)) + 12;
        if (nw > cw) cw = nw;
    }
    if (cw > host_cols) cw = host_cols;
    if (cw < 26) cw = 26;
    int ch = g_chooser_item_count + 3;

    if (w) *w = cw;
    if (h) *h = ch;
    *top = 2;   // below the tab bar
    *left = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
    if (*left + cw > host_cols) *left = (g_pop_anchor_x >= 0 ? g_pop_anchor_x : g_mouse_x) - cw;
    if (*left < 0) *left = 0;
}

static void render_chooser(char *out, int bs, int *posp, int host_rows, int host_cols) {
    int top, left, cw, ch;
    chooser_geom(host_rows, host_cols, &top, &left, &cw, &ch);
    int pos = *posp;

    // Header: ┌─ 新建 pane ──────┐
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[38;2;255;255;255m\x1b[48;2;31;111;235m┌─ 新建 pane ", top, left);
    int used = 2 + 11; // "┌─" (2) + " 新建 pane " (11)
    while (used < cw - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80';
        used++;
    }
    pos += snprintf(out + pos, bs - pos, "┐\x1b[0m");

    // Items
    for (int i = 0; i < g_chooser_item_count; i++) {
        int r = top + 1 + i;
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m  \x1b[38;2;210;153;34m[%d]\x1b[0m \x1b[38;2;230;237;243m%s\x1b[0m",
                        r, left, i + 1, g_chooser_items[i].name);
        int item_used = 1 + 2 + 3 + 1 + utf8_cols(g_chooser_items[i].name, (int)strlen(g_chooser_items[i].name));
        while (item_used < cw - 1 && pos < bs - 8) { out[pos++] = ' '; item_used++; }
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;33;38;45m│\x1b[0m");
    }

    // Esc row
    int esc_r = top + 1 + g_chooser_item_count;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m  \x1b[38;2;139;148;158mEsc 取消\x1b[0m", esc_r, left);
    int esc_used = 1 + 2 + 8; // "│" (1) + "  " (2) + "Esc 取消" (8)
    while (esc_used < cw - 1 && pos < bs - 8) { out[pos++] = ' '; esc_used++; }
    pos += snprintf(out + pos, bs - pos, "\x1b[48;2;33;38;45m│\x1b[0m");

    // Bottom border: └──────┘
    int bot_r = top + 2 + g_chooser_item_count;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m└", bot_r, left);
    int bot_used = 1;
    while (bot_used < cw - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80';
        bot_used++;
    }
    pos += snprintf(out + pos, bs - pos, "┘\x1b[0m");

    *posp = pos;
}

#define CMD_BOX_W 38
#define CMD_BOX_H 4
static void render_custom_cmd_box(char *out, int bs, int *posp, int host_rows, int host_cols) {
    (void)host_rows;
    int pos = *posp;
    int top = 2;   // below the tab bar
    int ax = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
    int left = ax;
    if (left + CMD_BOX_W > host_cols) left = ax - CMD_BOX_W;
    if (left < 0) left = 0;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[38;2;255;255;255m\x1b[48;2;31;111;235m┌─ 自定义命令行 ─────────────────────┐\x1b[0m", top, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m \x1b[38;2;230;237;243m%s\x1b[0m", top + 1, left, g_mux.custom_cmd_buf);
    int used = 2 + utf8_cols(g_mux.custom_cmd_buf, (int)strlen(g_mux.custom_cmd_buf));
    while (used < CMD_BOX_W - 1 && pos < bs - 8) { out[pos++] = ' '; used++; }
    pos += snprintf(out + pos, bs - pos, "\x1b[48;2;33;38;45m│\x1b[0m");
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m└────────────────────────────────────┘\x1b[0m", top + 2, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[30;43m[Enter=启动 Esc=取消]\x1b[0m", top + 3, left);
    *posp = pos;
}
// v8.46: rename box - a bordered dialog where the new title is typed.
#define RENAME_W 30
#define RENAME_H 3
static void render_rename_box(char *out, int bs, int *posp, int host_rows, int host_cols) {
    (void)host_rows;
    int pos = *posp;
    int top = 2;   // v8.47: below the tab bar
    int ax = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
    int left = ax;
    if (left + RENAME_W > host_cols) left = ax - RENAME_W;
    if (left < 0) left = 0;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[38;2;255;255;255m\x1b[48;2;31;111;235m┌─ 新标题 ───────────────────┐\x1b[0m", top, left);
    // input row: │  + typed text + padding + │
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m \x1b[38;2;230;237;243m%s\x1b[0m", top + 1, left, g_mux.rename_buf);
    // v8.50: pad by DISPLAY columns (utf8_cols), not bytes - CJK chars are 2
    // cols but 3 bytes; using strlen made the right border drift left by 1 per
    // CJK char.
    int used = 2 + utf8_cols(g_mux.rename_buf, (int)strlen(g_mux.rename_buf));
    while (used < RENAME_W - 1 && pos < bs - 8) { out[pos++] = ' '; used++; }
    pos += snprintf(out + pos, bs - pos, "\x1b[48;2;33;38;45m│\x1b[0m");
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m└────────────────────────────┘\x1b[0m", top + 2, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[30;43m[Enter=确认 Esc=取消]\x1b[0m", top + 3, left);   // v8.47: clear line
    *posp = pos;
}

// v8.33: right-click context menu for a tab: change color / rename.
#define CTX_W 24
#define CTX_H 4
static void render_ctx_menu(char *out, int bs, int *posp, int host_rows, int host_cols) {
    (void)host_rows;
    int pos = *posp;
    // v8.47: top = row 2 (below the tab bar) so the ┌──┐ border is visible
    int top = 2;
    int ax = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
    int left = ax;
    if (left + CTX_W > host_cols) left = ax - CTX_W;
    if (left < 0) left = 0;
    // v8.40: every row is exactly CTX_W (24) columns
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[38;2;255;255;255m\x1b[48;2;31;111;235m┌─ 标签菜单 ───────────┐\x1b[0m", top, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m  \x1b[38;2;210;153;34m[1]\x1b[0m \x1b[38;2;230;237;243m改颜色\x1b[0m          \x1b[48;2;33;38;45m│\x1b[0m", top + 1, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m  \x1b[38;2;210;153;34m[2]\x1b[0m \x1b[38;2;230;237;243m改标题\x1b[0m          \x1b[48;2;33;38;45m│\x1b[0m", top + 2, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m└──────────────────────┘\x1b[0m", top + 3, left);
    *posp = pos;
}
// v8.52: ctx_geom removed - the mouse handler computes top/left inline to
// match render_ctx_menu exactly (CTX_W clamping).
// v8.33: color picker (8 swatches shown as colored squares + index)
#define CP_W 30
#define CP_H 4
static void render_color_picker(char *out, int bs, int *posp, int host_rows, int host_cols) {
    (void)host_rows;
    int pos = *posp;
    // v8.47: top = row 2 (below the tab bar)
    int top = 2;
    int ax = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
    int left = ax;
    if (left + CP_W > host_cols) left = ax - CP_W;
    if (left < 0) left = 0;
    // v8.40: exact 30-col rows
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[38;2;255;255;255m\x1b[48;2;31;111;235m┌─ 选择颜色 ─────────────────┐\x1b[0m", top, left);
    // v8.50: swatches with hover highlight (bright bg + white text on hover,
    // dim bg + gray text otherwise). Each swatch = "  " block + digit + space.
    static const int cpsw[8][3] = {
        {31,111,235},{63,185,80},{210,153,34},{137,87,229},
        {31,136,61},{121,192,255},{217,119,54},{205,93,173}
    };
    static const int cpsw_dim[8][3] = {
        {22,62,128},{36,99,49},{110,82,30},{74,48,122},
        {24,80,48},{52,96,128},{112,66,34},{104,50,90}
    };
    for (int row = 0; row < 2; row++) {
        int y = top + 1 + row;
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m ", y, left);
        int x = left + 2;   // 1-based col of current swatch start
        for (int k = 0; k < 4; k++) {
            int ci = row * 4 + k;
            // v8.52: hover covers the FULL 4-column swatch (block+digit+gap).
            // x is the 1-based col of the swatch start; 0-based mouse col range
            // is [x-1, x+2] (4 columns), matching the click hit-test dc/4.
            int hover = (g_mouse_y == y - 1 && g_mouse_x >= x - 1 && g_mouse_x < x + 3);
            if (hover)
                pos += snprintf(out + pos, bs - pos, "\x1b[48;2;%d;%d;%dm  \x1b[0m\x1b[97;1m%d\x1b[0m ", cpsw[ci][0], cpsw[ci][1], cpsw[ci][2], ci + 1);
            else
                pos += snprintf(out + pos, bs - pos, "\x1b[48;2;%d;%d;%dm  \x1b[0m\x1b[90m%d\x1b[0m ", cpsw_dim[ci][0], cpsw_dim[ci][1], cpsw_dim[ci][2], ci + 1);
            x += 4;
        }
        // fill to right border
        while (x < left + CP_W - 1 && pos < bs - 8) { out[pos++] = ' '; x++; }
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;33;38;45m│\x1b[0m");
    }
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m└────────────────────────────┘\x1b[0m", top + 3, left);
    *posp = pos;
}

#define SET_W 68
static void settings_geom(int host_rows, int host_cols, int *top, int *left, int *w, int *h) {
    (void)host_rows;
    int sw = SET_W;
    if (sw > host_cols) sw = host_cols;
    if (sw < 40) sw = 40;
    int sh = g_chooser_item_count + 9;
    if (sh > host_rows) sh = host_rows;

    if (w) *w = sw;
    if (h) *h = sh;
    *top = 2;
    *left = (host_cols - sw) / 2;
    if (*left < 0) *left = 0;
}

static void render_settings_main(char *out, int bs, int *posp, int host_rows, int host_cols) {
    int top, left, sw, sh;
    settings_geom(host_rows, host_cols, &top, &left, &sw, &sh);
    int pos = *posp;

    // Header: ┌─ * termux 设置 ────────────────────────────┐
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[38;2;255;255;255m\x1b[48;2;137;87;229m┌─ * termux 设置 ", top, left);
    int cols = utf8_cols("┌─ * termux 设置 ", (int)strlen("┌─ * termux 设置 "));
    while (cols < sw - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80';
        cols++;
    }
    pos += snprintf(out + pos, bs - pos, "┐\x1b[0m");

    // Subheader: │  【新建菜单项配置】 (Ctrl+↑/↓选 / ↑/↓排 / Enter改 / Ctrl+D删)    │
    int r1 = top + 1;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m  \x1b[38;2;121;192;255;1m【新建菜单项配置】\x1b[0m \x1b[38;2;139;148;158m(Ctrl+↑/↓选 / ↑/↓排 / Enter改 / Ctrl+D删)\x1b[0m", r1, left);
    cols = 1 + 2 + utf8_cols("【新建菜单项配置】 (Ctrl+↑/↓选 / ↑/↓排 / Enter改 / Ctrl+D删)", (int)strlen("【新建菜单项配置】 (Ctrl+↑/↓选 / ↑/↓排 / Enter改 / Ctrl+D删)"));
    pad_to_right_border(out, bs, &pos, &cols, sw);

    // Column headers: │   #   显示名称       启动命令行                 操作           │
    int r2 = top + 2;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m   \x1b[38;2;210;153;34m#\x1b[0m   \x1b[38;2;230;237;243m显示名称       启动命令行                 操作\x1b[0m", r2, left);
    cols = 1 + 3 + 1 + 3 + utf8_cols("显示名称       启动命令行                 操作", (int)strlen("显示名称       启动命令行                 操作"));
    pad_to_right_border(out, bs, &pos, &cols, sw);

    // Divider: │  ────────────────────────────────────────────────────────────  │
    int r3 = top + 3;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m  ", r3, left);
    cols = 1 + 2;
    while (cols < sw - 3 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80';
        cols++;
    }
    pos += snprintf(out + pos, bs - pos, "  \x1b[48;2;33;38;45m│\x1b[0m");

    // Rows for items
    for (int i = 0; i < g_chooser_item_count; i++) {
        int r = top + 4 + i;
        int is_sel = (i == g_mux.settings_sel);
        int row_hover = (g_mouse_y == r - 1);
        int h_up = (row_hover && g_mouse_x >= left + 45 && g_mouse_x <= left + 47);
        int h_dn = (row_hover && g_mouse_x >= left + 48 && g_mouse_x <= left + 50);
        int h_ed = (row_hover && g_mouse_x >= left + 52 && g_mouse_x <= left + 55);
        int h_del = (row_hover && g_mouse_x >= left + 57 && g_mouse_x <= left + 60);

        const char *bg = is_sel ? "\x1b[48;2;45;55;72m" : "";
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m%s  \x1b[38;2;210;153;34m[%d]\x1b[0m%s \x1b[38;2;230;237;243;1m",
                        r, left, bg, i + 1, bg);
        cols = 1 + 2 + 4;
        append_padded_utf8(out, bs, &pos, &cols, g_chooser_items[i].name, 14);
        pos += snprintf(out + pos, bs - pos, "\x1b[0m%s \x1b[38;2;139;148;158m", bg);
        cols += 1;
        append_padded_utf8(out, bs, &pos, &cols, g_chooser_items[i].cmd, 22);
        pos += snprintf(out + pos, bs - pos, "\x1b[0m%s ", bg);
        cols += 1;

        // [↑] button
        if (h_up)
            pos += snprintf(out + pos, bs - pos, "\x1b[48;2;63;185;80m\x1b[38;2;255;255;255;1m[↑]\x1b[0m%s", bg);
        else
            pos += snprintf(out + pos, bs - pos, "\x1b[38;2;63;185;80m[↑]\x1b[0m%s", bg);
        cols += 3;

        // [↓] button
        if (h_dn)
            pos += snprintf(out + pos, bs - pos, "\x1b[48;2;217;119;54m\x1b[38;2;255;255;255;1m[↓]\x1b[0m%s ", bg);
        else
            pos += snprintf(out + pos, bs - pos, "\x1b[38;2;217;119;54m[↓]\x1b[0m%s ", bg);
        cols += 4;

        // [改] button
        if (h_ed)
            pos += snprintf(out + pos, bs - pos, "\x1b[48;2;31;111;235m\x1b[38;2;255;255;255;1m[改]\x1b[0m%s ", bg);
        else
            pos += snprintf(out + pos, bs - pos, "\x1b[38;2;121;192;255m[改]\x1b[0m%s ", bg);
        cols += 5;

        // [删] button
        if (h_del)
            pos += snprintf(out + pos, bs - pos, "\x1b[48;2;248;81;73m\x1b[38;2;255;255;255;1m[删]\x1b[0m%s", bg);
        else
            pos += snprintf(out + pos, bs - pos, "\x1b[38;2;248;81;73m[删]\x1b[0m%s", bg);
        cols += 4;

        pad_to_right_border(out, bs, &pos, &cols, sw);
    }

    // Action buttons row: [+] 添加新条目   [P] 快速添加预设
    int btn_r = top + 4 + g_chooser_item_count;
    int h_add = (g_mouse_y == btn_r - 1 && g_mouse_x >= left + 2 && g_mouse_x <= left + 19);
    int h_pre = (g_mouse_y == btn_r - 1 && g_mouse_x >= left + 22 && g_mouse_x <= left + 41);

    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m  ", btn_r, left);
    cols = 1 + 2;
    if (h_add)
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;45;135;255m\x1b[38;2;255;255;255;1m [+] 添加新条目 \x1b[0m  ");
    else
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;31;111;235m\x1b[38;2;255;255;255;1m [+] 添加新条目 \x1b[0m  ");
    cols += 16 + 2;

    if (h_pre)
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;45;165;85m\x1b[38;2;255;255;255;1m [P] 快速添加预设 \x1b[0m");
    else
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;31;136;61m\x1b[38;2;255;255;255;1m [P] 快速添加预设 \x1b[0m");
    cols += 18;

    pad_to_right_border(out, bs, &pos, &cols, sw);

    // Divider 2
    int d2_r = top + 5 + g_chooser_item_count;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m  ", d2_r, left);
    cols = 1 + 2;
    while (cols < sw - 3 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80';
        cols++;
    }
    pos += snprintf(out + pos, bs - pos, "  \x1b[48;2;33;38;45m│\x1b[0m");

    // Footer: [Ctrl+S] 保存配置          [Esc] 取消并返回
    int f_r = top + 6 + g_chooser_item_count;
    int h_save = (g_mouse_y == f_r - 1 && g_mouse_x >= left + 2 && g_mouse_x <= left + 20);
    int h_esc = (g_mouse_y == f_r - 1 && g_mouse_x >= left + 31 && g_mouse_x <= left + 48);

    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m  ", f_r, left);
    cols = 1 + 2;
    if (h_save)
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;63;185;80m\x1b[38;2;255;255;255;1m [Ctrl+S] 保存配置 \x1b[0m          ");
    else
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;33;38;45m\x1b[38;2;63;185;80;1m [Ctrl+S] 保存配置 \x1b[0m          ");
    cols += utf8_cols(" [Ctrl+S] 保存配置 ", (int)strlen(" [Ctrl+S] 保存配置 ")) + 10;

    if (h_esc)
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;217;119;54m\x1b[38;2;255;255;255;1m [Esc] 取消并返回 \x1b[0m");
    else
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;33;38;45m\x1b[38;2;217;119;54;1m [Esc] 取消并返回 \x1b[0m");
    cols += utf8_cols(" [Esc] 取消并返回 ", (int)strlen(" [Esc] 取消并返回 "));

    pad_to_right_border(out, bs, &pos, &cols, sw);

    // Bottom border: └────────────────────────────────────────────────────────┘
    int b_r = top + 7 + g_chooser_item_count;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m└", b_r, left);
    cols = 1;
    while (cols < sw - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80';
        cols++;
    }
    pos += snprintf(out + pos, bs - pos, "┘\x1b[0m");

    *posp = pos;
}

#define EDIT_BOX_W 52
#define EDIT_BOX_H 6
static void render_settings_edit_dialog(char *out, int bs, int *posp, int host_rows, int host_cols) {
    (void)host_rows;
    int top = 3;
    int ew = EDIT_BOX_W;
    if (ew > host_cols) ew = host_cols;
    int left = (host_cols - ew) / 2;
    if (left < 0) left = 0;
    int pos = *posp;

    const char *title = (g_mux.settings_edit_idx >= 0) ? "┌─ [*] 编辑菜单项 " : "┌─ [*] 添加新菜单项 ";
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[38;2;255;255;255m\x1b[48;2;31;111;235m%s", top, left, title);
    int cols = utf8_cols(title, (int)strlen(title));
    while (cols < ew - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80';
        cols++;
    }
    pos += snprintf(out + pos, bs - pos, "┐\x1b[0m");

    // Field 0: 名称
    int f0_sel = (g_mux.settings_edit_field == 0);
    const char *f0_bg = f0_sel ? "\x1b[48;2;50;60;80m" : "\x1b[48;2;22;27;34m";
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m  \x1b[38;2;210;153;34m名称: \x1b[0m%s\x1b[38;2;230;237;243;1m ", top + 1, left, f0_bg);
    cols = 1 + 2 + 6 + 1;
    append_padded_utf8(out, bs, &pos, &cols, g_mux.settings_edit_name, 36);
    pos += snprintf(out + pos, bs - pos, " \x1b[0m");
    cols += 1;
    pad_to_right_border(out, bs, &pos, &cols, ew);

    // Field 1: 命令
    int f1_sel = (g_mux.settings_edit_field == 1);
    const char *f1_bg = f1_sel ? "\x1b[48;2;50;60;80m" : "\x1b[48;2;22;27;34m";
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m  \x1b[38;2;210;153;34m命令: \x1b[0m%s\x1b[38;2;230;237;243m ", top + 2, left, f1_bg);
    cols = 1 + 2 + 6 + 1;
    append_padded_utf8(out, bs, &pos, &cols, g_mux.settings_edit_cmd, 36);
    pos += snprintf(out + pos, bs - pos, " \x1b[0m");
    cols += 1;
    pad_to_right_border(out, bs, &pos, &cols, ew);

    // Tips: [Tab] 切换输入行
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m  \x1b[38;2;139;148;158m[Tab] 切换输入项  (:custom 为自定义命令行)\x1b[0m", top + 3, left);
    cols = 1 + 2 + utf8_cols("[Tab] 切换输入项  (:custom 为自定义命令行)", (int)strlen("[Tab] 切换输入项  (:custom 为自定义命令行)"));
    pad_to_right_border(out, bs, &pos, &cols, ew);

    // Action buttons: [Enter] 确定  [Esc] 取消
    int h_ok = (g_mouse_y == top + 3 && g_mouse_x >= left + 2 && g_mouse_x <= left + 19);
    int h_esc = (g_mouse_y == top + 3 && g_mouse_x >= left + 30 && g_mouse_x <= left + 41);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m  ", top + 4, left);
    cols = 1 + 2;
    if (h_ok)
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;63;185;80m\x1b[38;2;255;255;255;1m [Enter] 确认保存 \x1b[0m          ");
    else
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;33;38;45m\x1b[38;2;63;185;80;1m [Enter] 确认保存 \x1b[0m          ");
    cols += 18 + 10;

    if (h_esc)
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;217;119;54m\x1b[38;2;255;255;255;1m [Esc] 取消 \x1b[0m");
    else
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;33;38;45m\x1b[38;2;217;119;54;1m [Esc] 取消 \x1b[0m");
    cols += 12;

    pad_to_right_border(out, bs, &pos, &cols, ew);

    // Bottom border
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m└", top + 5, left);
    cols = 1;
    while (cols < ew - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80';
        cols++;
    }
    pos += snprintf(out + pos, bs - pos, "┘\x1b[0m");

    *posp = pos;
}

#define PRESET_BOX_W 50
static void render_settings_presets(char *out, int bs, int *posp, int host_rows, int host_cols) {
    (void)host_rows;
    int top = 3;
    int pw = PRESET_BOX_W;
    if (pw > host_cols) pw = host_cols;
    int left = (host_cols - pw) / 2;
    if (left < 0) left = 0;
    int pos = *posp;

    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[38;2;255;255;255m\x1b[48;2;31;136;61m┌─ 常用命令行预设 (按数字添加) ", top, left);
    int cols = utf8_cols("┌─ 常用命令行预设 (按数字添加) ", (int)strlen("┌─ 常用命令行预设 (按数字添加) "));
    while (cols < pw - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80';
        cols++;
    }
    pos += snprintf(out + pos, bs - pos, "┐\x1b[0m");

    for (int i = 0; i < g_preset_count; i++) {
        int r = top + 1 + i;
        int row_hover = (g_mouse_y == r - 1);
        const char *bg = row_hover ? "\x1b[48;2;45;55;72m" : "";
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m%s  \x1b[38;2;210;153;34m[%d]\x1b[0m%s \x1b[38;2;230;237;243;1m",
                        r, left, bg, i + 1, bg);
        cols = 1 + 2 + 4;
        append_padded_utf8(out, bs, &pos, &cols, g_presets[i].name, 12);
        pos += snprintf(out + pos, bs - pos, "\x1b[0m%s \x1b[38;2;139;148;158m", bg);
        cols += 1;
        append_padded_utf8(out, bs, &pos, &cols, g_presets[i].cmd, 26);
        pos += snprintf(out + pos, bs - pos, "\x1b[0m");
        pad_to_right_border(out, bs, &pos, &cols, pw);
    }

    int esc_r = top + 1 + g_preset_count;
    int h_esc = (g_mouse_y == esc_r - 1 && g_mouse_x >= left + 2 && g_mouse_x <= left + 13);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m│\x1b[0m  ", esc_r, left);
    cols = 1 + 2;
    if (h_esc)
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;217;119;54m\x1b[38;2;255;255;255;1m [Esc] 取消 \x1b[0m");
    else
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;33;38;45m\x1b[38;2;139;148;158m [Esc] 取消 \x1b[0m");
    cols += 12;
    pad_to_right_border(out, bs, &pos, &cols, pw);

    int bot_r = top + 2 + g_preset_count;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[K\x1b[48;2;33;38;45m└", bot_r, left);
    cols = 1;
    while (cols < pw - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80';
        cols++;
    }
    pos += snprintf(out + pos, bs - pos, "┘\x1b[0m");

    *posp = pos;
}

static void update_host_title(void) {
    const char *target = NULL;
    if (g_mux.help_mode) {
        target = "termux - 帮助";
    } else if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active) {
        const char *t = g_mux.panes[g_mux.active_pane].title;
        target = (t && t[0]) ? t : "cmd";
    } else {
        target = "termux";
    }
    if (strcmp(g_current_host_title, target) != 0) {
        strncpy(g_current_host_title, target, sizeof(g_current_host_title) - 1);
        g_current_host_title[sizeof(g_current_host_title) - 1] = 0;
        SetConsoleTitleA(g_current_host_title);
        char seq[256];
        int len = snprintf(seq, sizeof(seq), "\x1b]0;%s\x07", g_current_host_title);
        if (len > 0) host_write(seq, len);
    }
}

static void render_screen(void) {
    EnterCriticalSection(&g_mux.cs);
    if (g_mux.host_cols < 1 || g_mux.host_rows < 1 || g_mux.total_host_rows < 1) { LeaveCriticalSection(&g_mux.cs); return; }
    update_host_title();

    int bs = (g_mux.host_rows + 4) * (g_mux.host_cols * 48 + 1024) + 16384;
    char *out = render_buffer_acquire(bs);
    if (!out) { LeaveCriticalSection(&g_mux.cs); return; }
    int pos = 0;

    pos += snprintf(out + pos, bs - pos, "\x1b[?25l");

    // 1. Render background content (Help view or Active Pane)
    if (g_mux.help_mode) {
        render_help_content(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    } else if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active) {
        Pane *pane = &g_mux.panes[g_mux.active_pane];
        ScreenBuffer *s = &pane->screen;
        WORD la_attr = 0xFFFF, la_fr = 0, la_br = 0; int la_fv = -1, la_bv = -1;
        // v8.14: defensive clamp - never render rows beyond the real history depth
        if (pane->scroll_offset > s->hist_lines) pane->scroll_offset = s->hist_lines;
        if (pane->scroll_offset < 0) pane->scroll_offset = 0;
        int vo = pane->scroll_offset, rr = s->rows < g_mux.host_rows ? s->rows : g_mux.host_rows, rc = s->cols < g_mux.host_cols ? s->cols : g_mux.host_cols;

        for (int y = 0; y < rr; y++) {
            pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H", y + 2);   // v8.22: tab bar is on row 1
            for (int x = 0; x < rc; x++) {
                CHAR_INFO *cell = NULL;
                int ar = -1;
                if (vo > 0 && !s->in_alt_screen) { ar = s->scroll_top + y - vo; if (ar >= 0 && ar < s->total_lines) cell = &s->buffer[ar * s->cols + x]; }
                else cell = screen_cell(s, y, x);
                WCHAR wc = L' '; WORD attr = 0x07;
                if (cell) { wc = cell->Char.UnicodeChar; attr = cell->Attributes; }
                if (wc == 0) continue;   // v8.2: right half of a wide char
                WORD frgb, brgb; int fgv, bgv;
                cell_truecolor(s, y, x, ar, &frgb, &brgb, &fgv, &bgv);
                if (attr != la_attr || frgb != la_fr || brgb != la_br || fgv != la_fv || bgv != la_bv) {
                    const char *ul = (attr & COMMON_LVB_UNDERSCORE) ? ";4" : "";
                    if (fgv || bgv) {
                        int fr, fg2, fb; rgb565_split(frgb, &fr, &fg2, &fb);
                        int br2, bg2, bb; rgb565_split(brgb, &br2, &bg2, &bb);
                        if (fgv && bgv)
                            pos += snprintf(out + pos, bs - pos, "\x1b[0%s;38;2;%d;%d;%d;48;2;%d;%d;%dm", ul, fr, fg2, fb, br2, bg2, bb);
                        else if (fgv)
                            pos += snprintf(out + pos, bs - pos, "\x1b[0%s;38;2;%d;%d;%dm", ul, fr, fg2, fb);
                        else
                            pos += snprintf(out + pos, bs - pos, "\x1b[0%s;48;2;%d;%d;%dm", ul, br2, bg2, bb);
                    } else {
                        static const int m[8] = {0,4,2,6,1,5,3,7};
                        int fg = attr & 0x0F, bg = (attr >> 4) & 0x0F;
                        if (fg & 8) pos += snprintf(out + pos, bs - pos, "\x1b[0%s;1;%d;%dm", ul, (fg & 8) ? 90 + m[fg & 7] : 30 + m[fg & 7], (bg & 8) ? 100 + m[bg & 7] : 40 + m[bg & 7]);
                        else pos += snprintf(out + pos, bs - pos, "\x1b[0%s;%d;%dm", ul, 30 + m[fg & 7], 40 + m[bg & 7]);
                    }
                    la_attr = attr; la_fr = frgb; la_br = brgb; la_fv = fgv; la_bv = bgv;
                }
                if (wc >= 0xD800 && wc <= 0xDBFF && x + 1 < rc) {
                    CHAR_INFO *next_cell = NULL;
                    if (vo > 0 && !s->in_alt_screen) { if (ar >= 0 && ar < s->total_lines) next_cell = &s->buffer[ar * s->cols + x + 1]; }
                    else next_cell = screen_cell(s, y, x + 1);
                    if (next_cell && next_cell->Char.UnicodeChar >= 0xDC00 && next_cell->Char.UnicodeChar <= 0xDFFF) {
                        WCHAR low = next_cell->Char.UnicodeChar;
                        unsigned int cp = 0x10000 + (((unsigned int)(wc & 0x3FF)) << 10) + (low & 0x3FF);
                        out[pos++] = (char)(0xF0 | (cp >> 18));
                        out[pos++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                        out[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        out[pos++] = (char)(0x80 | (cp & 0x3F));
                        x++; // skip low surrogate cell
                        if (pos > bs - 256) break;
                        continue;
                    }
                }
                if (wc < 0x80) out[pos++] = (char)wc;
                else if (wc < 0x800) { out[pos++] = 0xC0 | (wc >> 6); out[pos++] = 0x80 | (wc & 0x3F); }
                else { out[pos++] = 0xE0 | (wc >> 12); out[pos++] = 0x80 | ((wc >> 6) & 0x3F); out[pos++] = 0x80 | (wc & 0x3F); }
                if (pos > bs - 256) break;
            }
            pos += snprintf(out + pos, bs - pos, "\x1b[K");
            if (pos > bs - 256) break;
        }

        // clear leftover rows below pane
        for (int y = rr; y < g_mux.host_rows && pos < bs - 64; y++)
            pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H\x1b[K", y + 2);

        if (vo > 0) { int pct = s->hist_lines > 0 ? (vo * 100 / s->hist_lines) : 0; pos += snprintf(out + pos, bs - pos, "\x1b[2;%dH\x1b[30;43m[%d%%]\x1b[0m", g_mux.host_cols - 10, pct); }
    } else {
        for (int y = 0; y < g_mux.host_rows && pos < bs - 64; y++)
            pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H\x1b[K", y + 2);
    }

    // 2. Overlay popups (so popups cleanly replace each other without leftover rows)
    if (g_mux.chooser_mode) {
        render_chooser(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    } else if (g_mux.ctx_mode == 1) {
        render_ctx_menu(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    } else if (g_mux.ctx_mode == 2) {
        render_color_picker(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    } else if (g_mux.rename_mode) {
        render_rename_box(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    } else if (g_mux.custom_cmd_mode) {
        render_custom_cmd_box(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    } else if (g_mux.settings_mode == 1) {
        render_settings_main(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    } else if (g_mux.settings_mode == 2) {
        render_settings_edit_dialog(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    } else if (g_mux.settings_mode == 3) {
        render_settings_presets(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    }

    // 3. Tab bar at top
    pos += snprintf(out + pos, bs - pos, "\x1b[0m\x1b[1;1H");
    draw_tab_bar(out, bs, &pos);

    // 4. Position and set cursor visibility as the FINAL step (must be after draw_tab_bar)
    if (g_mux.rename_mode) {
        int r_top = 2, r_left = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
        if (r_left + RENAME_W > g_mux.host_cols) r_left = (g_pop_anchor_x >= 0 ? g_pop_anchor_x : g_mouse_x) - RENAME_W;
        if (r_left < 0) r_left = 0;
        int cx = r_left + 2 + utf8_cols(g_mux.rename_buf, g_mux.rename_pos);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", r_top + 1, cx);
    } else if (g_mux.custom_cmd_mode) {
        int c_top = 2, c_left = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
        if (c_left + CMD_BOX_W > g_mux.host_cols) c_left = (g_pop_anchor_x >= 0 ? g_pop_anchor_x : g_mouse_x) - CMD_BOX_W;
        if (c_left < 0) c_left = 0;
        int cx = c_left + 2 + utf8_cols(g_mux.custom_cmd_buf, g_mux.custom_cmd_pos);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", c_top + 1, cx);
    } else if (g_mux.settings_mode == 2) {
        int top = 3;
        int ew = EDIT_BOX_W;
        if (ew > g_mux.host_cols) ew = g_mux.host_cols;
        int left = (g_mux.host_cols - ew) / 2;
        if (left < 0) left = 0;
        if (g_mux.settings_edit_field == 0) {
            int cx = left + 1 + 2 + 6 + 1 + utf8_cols(g_mux.settings_edit_name, g_mux.settings_edit_name_pos);
            pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", top + 1, cx);
        } else {
            int cx = left + 1 + 2 + 6 + 1 + utf8_cols(g_mux.settings_edit_cmd, g_mux.settings_edit_cmd_pos);
            pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", top + 2, cx);
        }
    } else if (g_mux.chooser_mode || g_mux.ctx_mode || g_mux.help_mode || g_mux.settings_mode) {
        pos += snprintf(out + pos, bs - pos, "\x1b[?25l");
    } else if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active) {
        Pane *pane = &g_mux.panes[g_mux.active_pane];
        ScreenBuffer *s = &pane->screen;
        int vo = pane->scroll_offset;
        int rr = s->rows < g_mux.host_rows ? s->rows : g_mux.host_rows;
        int rc = s->cols < g_mux.host_cols ? s->cols : g_mux.host_cols;
        if (vo == 0 && s->cursor_visible && s->cursor_y + 1 <= rr && s->cursor_x + 1 <= rc) {
            pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", s->cursor_y + 2, s->cursor_x + 1);
        } else {
            pos += snprintf(out + pos, bs - pos, "\x1b[?25l");
        }
    } else {
        pos += snprintf(out + pos, bs - pos, "\x1b[?25l");
    }

    host_write(out, pos);
    if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active)
        dump_render_output(out, pos, g_mux.panes[g_mux.active_pane].screen.cols, g_mux.panes[g_mux.active_pane].screen.rows, g_mux.host_cols, g_mux.host_rows);
    g_mux.needs_redraw = 0;
    LeaveCriticalSection(&g_mux.cs);
}

// ============================================================
// Pane management
// ============================================================
static void write_to_pane_internal(Pane *pane, const char *data, int len) { if (!pane || !pane->active) return; DWORD w; WriteFile(pane->pipe_in, data, len, &w, NULL); }
static void write_to_pane(const char *data, int len) { if (g_mux.active_pane < 0 || g_mux.active_pane >= g_mux.pane_count) return; write_to_pane_internal(&g_mux.panes[g_mux.active_pane], data, len); }

// v8: mark a pane dead; if it was the active pane, switch to the next live one.
// Safe to call from the read thread or from the main loop (takes the lock).
static void pane_mark_dead(int idx) {
    if (idx < 0 || idx >= MAX_PANES) return;
    EnterCriticalSection(&g_mux.cs);
    Pane *pane = &g_mux.panes[idx];
    if (!pane->active) { LeaveCriticalSection(&g_mux.cs); return; }
    pane->active = 0;
    int next = -1;
    for (int i = 0; i < g_mux.pane_count; i++) if (g_mux.panes[i].active && i != idx) { next = i; break; }
    if (idx == g_mux.active_pane) {
        if (next >= 0) { g_mux.active_pane = next; g_mux.panes[next].scroll_offset = 0; }
        else g_mux.running = 0;
    }
    g_mux.needs_redraw = 1;
    LeaveCriticalSection(&g_mux.cs);
}

static unsigned __stdcall pane_read_thread(void *arg) {
    int idx = (int)(intptr_t)arg;
    Pane *pane = &g_mux.panes[idx];
    char buf[READ_BUF_SIZE];
    while (pane->active) {
        DWORD br = 0;
        if (!ReadFile(pane->pipe_out, buf, sizeof(buf), &br, NULL) || br == 0) break;
        dump_pane_bytes(idx, buf, (int)br);   // v8.2: diagnostic raw dump
        EnterCriticalSection(&g_mux.cs);
        screen_process_output(&pane->screen, buf, br);
        if (pane->screen.response_len > 0) { write_to_pane_internal(pane, pane->screen.response_buf, pane->screen.response_len); pane->screen.response_len = 0; }
        if (idx == g_mux.active_pane) g_mux.needs_redraw = 1;
        LeaveCriticalSection(&g_mux.cs);
    }
    // v8: pane is dead (EOF, broken pipe, or close_pane closed the pipe out
    // from under us). Mark it dead / switch the active pane; the main loop
    // reaps the resources via close_pane().
    pane_mark_dead(idx);
    return 0;
}

// v8.19: help content as a line table so the view can scroll (PgUp/PgDn or
// mouse wheel). Termux renders it itself - no cmd process involved.
static const char *const g_help_lines[] = {
    "\x1b[38;2;255;255;255m\x1b[48;2;31;111;235m termux - 帮助",
    "\x1b[38;2;139;148;158m  版本 v1.1.1 | Windows Terminal Multiplexer (Win10 1809+)\x1b[0m",
    "",
    "\x1b[38;2;121;192;255;1m  键盘快捷键\x1b[0m",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243mc\x1b[0m         新建默认 pane",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243mn / p\x1b[0m     下一个 / 上一个 pane",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243mx\x1b[0m         关闭当前 pane",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243ms\x1b[0m         打开图形化设置 (termux.ini)",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243md\x1b[0m         退出 termux",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243mt\x1b[0m         轮换标签颜色 (Shift+t 反向)",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243m0-9\x1b[0m       跳转到 pane",
    "",
    "\x1b[38;2;121;192;255;1m  鼠标操作\x1b[0m",
    "  \x1b[38;2;230;237;243m点击 tab\x1b[0m           切换 pane",
    "  \x1b[38;2;230;237;243m点击 [x]\x1b[0m           关闭该 pane",
    "  \x1b[38;2;230;237;243m右键 tab\x1b[0m           改颜色 / 改标题",
    "  \x1b[38;2;230;237;243m点击 [+]\x1b[0m           新建 pane (支持选择/自定义命令行)",
    "  \x1b[38;2;230;237;243m点击 [*]\x1b[0m           打开图形化设置页面",
    "  \x1b[38;2;230;237;243m点击 termux\x1b[0m       打开 / 关闭本帮助",
    "",
    "\x1b[38;2;121;192;255;1m  提示与警告\x1b[0m",
    "  - \x1b[38;2;248;81;73m警告: 终端必须使用等宽字体，否则会渲染故障\x1b[0m",
    "  - 每个 tab 带 \x1b[38;2;248;81;73m红 x\x1b[0m 关闭按钮（悬停红底）",
    "  - 编辑器 (nano/vim) 用 alt screen，退出后历史完整保留",
    "  - PgUp / PgDn / 滚轮可滚动本帮助",
    "  - 按任意其它键返回",
};
static const int g_help_line_count = (int)(sizeof(g_help_lines) / sizeof(g_help_lines[0]));

static void render_help_content(char *out, int bs, int *posp, int host_rows, int host_cols) {
    (void)host_cols;
    int pos = *posp;
    // v8.19/v8.22: clamp scroll to the visible range (content starts at row 2; tab bar is row 1)
    int vis = host_rows;
    int max_sc = g_help_line_count - vis;
    if (max_sc < 0) max_sc = 0;
    if (g_mux.help_scroll > max_sc) g_mux.help_scroll = max_sc;
    if (g_mux.help_scroll < 0) g_mux.help_scroll = 0;
    for (int r = 0; r < vis; r++) {
        int li = g_mux.help_scroll + r;
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H\x1b[K", r + 2);   // v8.22
        if (li < g_help_line_count)
            pos += snprintf(out + pos, bs - pos, "%s", g_help_lines[li]);
    }
    // scroll position indicator (only when the content overflows)
    if (max_sc > 0) {
        int pct = (g_mux.help_scroll * 100) / (max_sc + 1);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[30;43m[%d%%]\x1b[0m", g_mux.total_host_rows, host_cols - 8, pct);   // v8.22: bottom row
    }
    *posp = pos;
}

static int create_pane_shell(const WCHAR *shell) {
    // A dead pane may still be waiting for the main loop to reap its reader and
    // handles. Only reuse a completely clean slot; otherwise memset would lose
    // those resources and could race the old reader thread.
    int idx = -1;
    for (int i = 0; i < MAX_PANES; i++)
        if (!g_mux.panes[i].active && g_mux.panes[i].read_thread == NULL) { idx = i; break; }
    if (idx < 0) return -1;

    Pane *pane = &g_mux.panes[idx]; memset(pane, 0, sizeof(*pane));
    if (!screen_init(&pane->screen, g_mux.host_cols, g_mux.host_rows)) return -1;
    pane->screen.pane_index = idx;   // v7: needed for OSC title routing

    HANDLE pi_r = NULL, pi_w = NULL, po_r = NULL, po_w = NULL;
    COORD sz = {(SHORT)g_mux.host_cols, (SHORT)g_mux.host_rows};
    STARTUPINFOEXW si = {0};
    SIZE_T as = 0;
    PROCESS_INFORMATION pi = {0};
    WCHAR cmdline[256] = {0};
    BOOL created = FALSE;
    si.StartupInfo.cb = sizeof(si);

    if (!CreatePipe(&pi_r, &pi_w, NULL, 0)) goto create_fail;
    if (!CreatePipe(&po_r, &po_w, NULL, 0)) goto create_fail;
    if (FAILED(CreatePseudoConsole(sz, pi_r, po_w, 0, &pane->hpc))) goto create_fail;


    InitializeProcThreadAttributeList(NULL, 1, 0, &as);
    if (as == 0) goto create_fail;
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(as);
    if (!si.lpAttributeList) goto create_fail;
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &as)) goto attr_fail;
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pane->hpc, sizeof(HPCON), NULL, NULL)) {
        DeleteProcThreadAttributeList(si.lpAttributeList);
        goto attr_fail;
    }

    // v8.21: shell is now selectable (cmd / powershell / custom); copy into a writable buffer
    wcsncpy(cmdline, shell, 255); cmdline[255] = 0;
    created = CreateProcessW(NULL, cmdline, NULL, NULL, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                             &si.StartupInfo, &pi);
    // If direct execution failed, try via cmd.exe /c
    if (!created && _wcsicmp(shell, L"cmd.exe") != 0 && _wcsicmp(shell, L"powershell.exe") != 0) {
        WCHAR fallback[300];
        _snwprintf(fallback, 299, L"cmd.exe /c %s", shell);
        created = CreateProcessW(NULL, fallback, NULL, NULL, FALSE,
                                 EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                                 &si.StartupInfo, &pi);
    }
    DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);
    si.lpAttributeList = NULL;
    if (!created) {
        DWORD err = GetLastError();
        CloseHandle(pi_r); pi_r = NULL;
        CloseHandle(po_w); po_w = NULL;
        pane->pipe_in = pi_w; pane->pipe_out = po_r;
        pane->active = 1;
        pane->exited_hold = 1;
        pane->exit_code = err;
        char u8cmd[64] = {0};
        WideCharToMultiByte(CP_UTF8, 0, shell, -1, u8cmd, 63, NULL, NULL);
        char *space = strchr(u8cmd, ' ');
        if (space) *space = 0;
        sanitize_title(u8cmd, (int)strlen(u8cmd), pane->title, sizeof(pane->title));

        char errmsg[256];
        int elen = snprintf(errmsg, sizeof(errmsg),
            "\x1b[31;1m[启动失败: 无法执行命令 \"%s\" (错误码: %lu)]\x1b[0m\r\n\x1b[33m按任意键关闭该标签页...\x1b[0m\r\n",
            u8cmd, (unsigned long)err);
        EnterCriticalSection(&g_mux.cs);
        screen_process_output(&pane->screen, errmsg, elen);
        LeaveCriticalSection(&g_mux.cs);

        if (idx >= g_mux.pane_count) g_mux.pane_count = idx + 1;
        return idx;
    }

    CloseHandle(pi_r); pi_r = NULL;
    CloseHandle(po_w); po_w = NULL;
    pane->pipe_in = pi_w; pane->pipe_out = po_r; pane->process = pi.hProcess; pane->thread = pi.hThread; pane->active = 1;
    if (_wcsicmp(shell, L"powershell.exe") == 0 || _wcsicmp(shell, L"powershell") == 0) {
        strcpy(pane->title, "PowerShell");
    } else if (_wcsicmp(shell, L"cmd.exe") == 0 || _wcsicmp(shell, L"cmd") == 0) {
        strcpy(pane->title, "cmd");
    } else {
        char u8cmd[64] = {0};
        WideCharToMultiByte(CP_UTF8, 0, shell, -1, u8cmd, 63, NULL, NULL);
        char *space = strchr(u8cmd, ' ');
        if (space) *space = 0;
        sanitize_title(u8cmd, (int)strlen(u8cmd), pane->title, sizeof(pane->title));
    }
    if (idx >= g_mux.pane_count) g_mux.pane_count = idx + 1;
    pane->read_thread = (HANDLE)_beginthreadex(NULL, 0, pane_read_thread, (void*)(intptr_t)idx, 0, NULL);
    if (!pane->read_thread) {
        // v7: thread creation failed - undo everything
        pane->active = 0;
        ClosePseudoConsole(pane->hpc);
        CloseHandle(pane->pipe_in); CloseHandle(pane->pipe_out);
        TerminateProcess(pane->process, 0); WaitForSingleObject(pane->process, 500);
        CloseHandle(pane->process); CloseHandle(pane->thread);
        screen_free(&pane->screen);
        return -1;
    }
    return idx;

attr_fail:
    free(si.lpAttributeList);
create_fail:
    if (pane->hpc) ClosePseudoConsole(pane->hpc);
    if (pi_r) CloseHandle(pi_r);
    if (pi_w) CloseHandle(pi_w);
    if (po_r) CloseHandle(po_r);
    if (po_w) CloseHandle(po_w);
    screen_free(&pane->screen);
    memset(pane, 0, sizeof(*pane));
    return -1;
}

// v8.21: default shell is configured item 1 or cmd
static int create_pane(void) {
    if (g_chooser_item_count > 0 && strcmp(g_chooser_items[0].cmd, ":custom") != 0) {
        WCHAR wcmd[256] = {0};
        MultiByteToWideChar(CP_UTF8, 0, g_chooser_items[0].cmd, -1, wcmd, 255);
        return create_pane_shell(wcmd);
    }
    return create_pane_shell(L"cmd.exe");
}

static void close_pane(int idx) {
    if (idx < 0 || idx >= g_mux.pane_count) return;
    Pane *pane = &g_mux.panes[idx];

    // v8: detach under the lock so a running read thread can never touch a
    // pane that is being torn down (no use-after-free of the screen buffers).
    EnterCriticalSection(&g_mux.cs);
    if (!pane->active && !pane->read_thread) { LeaveCriticalSection(&g_mux.cs); return; }   // already fully closed
    pane->active = 0;
    LeaveCriticalSection(&g_mux.cs);

    ClosePseudoConsole(pane->hpc);
    CloseHandle(pane->pipe_in);
    CloseHandle(pane->pipe_out);   // aborts any pending ReadFile in the read thread
    if (pane->read_thread) {
        WaitForSingleObject(pane->read_thread, 2000);   // v7: a bit more patience
        CloseHandle(pane->read_thread);
        pane->read_thread = NULL;
    }
    TerminateProcess(pane->process, 0);   // no-op if the child already exited
    WaitForSingleObject(pane->process, 500);
    CloseHandle(pane->process);
    CloseHandle(pane->thread);

    // v8: free the buffers only after the reader is done with them.
    EnterCriticalSection(&g_mux.cs);
    screen_free(&pane->screen);
    LeaveCriticalSection(&g_mux.cs);
}

static void switch_pane(int idx) { if (idx < 0 || idx >= g_mux.pane_count || !g_mux.panes[idx].active) return; g_mux.active_pane = idx; g_mux.panes[idx].scroll_offset = 0; g_mux.needs_redraw = 1; }
static int find_next_active_pane(int cur) { for (int i = 1; i <= g_mux.pane_count; i++) { int n = (cur + i) % g_mux.pane_count; if (g_mux.panes[n].active) return n; } return -1; }

// v8: called from the main loop every ~30ms.
//  - Detects child-process exit via the PROCESS HANDLE. Relying on pipe EOF is
//    unreliable with ConPTY: the host keeps the output pipe's write end open,
//    so ReadFile can block forever and a dead pane would never close.
//  - Reaps panes whose read thread already finished (releases handles/buffers).
static void reap_dead_panes(void) {
    for (int i = 0; i < g_mux.pane_count; i++) {
        Pane *p = &g_mux.panes[i];
        if (!p->active) {
            if (p->read_thread != NULL) close_pane(i);   // reader finished: release resources
            continue;
        }
        if (p->exited_hold) {
            continue;   // Waiting for user keypress/click to close
        }
        if (p->process != NULL && WaitForSingleObject(p->process, 0) == WAIT_OBJECT_0) {
            DWORD exit_code = 0;
            GetExitCodeProcess(p->process, &exit_code);
            if (exit_code != 0) {
                // Non-zero exit code! Drain remaining output and show error prompt
                if (p->read_thread != NULL)
                    WaitForSingleObject(p->read_thread, 250);
                char msg[256];
                int mlen = snprintf(msg, sizeof(msg),
                    "\r\n\x1b[31;1m[进程异常退出，退出码: %lu (0x%lX)]\x1b[0m \x1b[33m按任意键关闭该标签页...\x1b[0m\r\n",
                    (unsigned long)exit_code, (unsigned long)exit_code);
                EnterCriticalSection(&g_mux.cs);
                screen_process_output(&p->screen, msg, mlen);
                p->exited_hold = 1;
                p->exit_code = exit_code;
                g_mux.needs_redraw = 1;
                LeaveCriticalSection(&g_mux.cs);
                continue;
            }
            // Child exited normally (code 0)
            if (p->read_thread != NULL)
                WaitForSingleObject(p->read_thread, 250);
            pane_mark_dead(i);
            close_pane(i);
        }
    }
}

// ============================================================
// Input handling
// ============================================================
static void do_scroll(int d) {
    if (g_mux.active_pane < 0) return;
    Pane *p = &g_mux.panes[g_mux.active_pane];
    if (!p->active || p->screen.in_alt_screen) return;
    // v8.14: no real history -> scrolling is forbidden entirely (never allow
    // scrolling into empty buffer).
    int mx = p->screen.hist_lines;
    if (mx <= 0) { p->scroll_offset = 0; return; }
    p->scroll_offset += d;
    if (p->scroll_offset > mx) p->scroll_offset = mx;
    if (p->scroll_offset < 0) p->scroll_offset = 0;
    g_mux.needs_redraw = 1;
}

static void handle_prefix(WORD vk, DWORD ctrl) {
    g_mux.prefix_mode = 0;
    if (vk == 'B' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) { char c = 2; write_to_pane(&c, 1); return; }
    switch (vk) {
        case 'C': case 'c': { int i = create_pane(); if (i >= 0) switch_pane(i); break; }
        case 'N': case 'n': { int n = find_next_active_pane(g_mux.active_pane); if (n >= 0) switch_pane(n); break; }
        case 'P': case 'p': { for (int i = 1; i <= g_mux.pane_count; i++) { int n = (g_mux.active_pane - i + g_mux.pane_count) % g_mux.pane_count; if (g_mux.panes[n].active) { switch_pane(n); break; } } break; }
        case 'X': case 'x': { int c = g_mux.active_pane, n = find_next_active_pane(c); close_pane(c); if (n >= 0 && g_mux.panes[n].active) switch_pane(n); else { int f = 0; for (int i = 0; i < g_mux.pane_count; i++) if (g_mux.panes[i].active) { switch_pane(i); f = 1; break; } if (!f) g_mux.running = 0; } break; }
        case 'D': case 'd': g_mux.running = 0; break;
        // v8.32: Ctrl+B + t cycles the current tab's color (Shift+t = reverse)
        case 'T': case 't': {
            if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count) {
                int c = g_mux.panes[g_mux.active_pane].color;
                c += (vk == 'T') ? -1 : 1;   // 'T'=Shift+t → prev, 't' → next
                if (c > 8) c = 1;
                if (c < 1) c = 8;
                g_mux.panes[g_mux.active_pane].color = c;
                g_mux.needs_redraw = 1;
            }
            break;
        }
        case 'S': case 's': {
            g_mux.chooser_mode = 0;
            g_mux.ctx_mode = 0;
            g_mux.rename_mode = 0;
            g_mux.custom_cmd_mode = 0;
            g_mux.help_mode = 0;
            g_mux.settings_mode = (g_mux.settings_mode ? 0 : 1);
            g_mux.settings_sel = 0;
            g_mux.needs_redraw = 1;
            break;
        }
        default: if (vk >= '0' && vk <= '9') { int i = vk - '0'; if (i < g_mux.pane_count && g_mux.panes[i].active) switch_pane(i); } break;
    }
}

static void handle_key(KEY_EVENT_RECORD *ke) {
    if (!ke->bKeyDown) {
        if (!g_mux.prefix_mode && !g_mux.settings_mode && !g_mux.rename_mode && !g_mux.custom_cmd_mode &&
            !g_mux.chooser_mode && !g_mux.ctx_mode && !g_mux.help_mode) {
            if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active) {
                Pane *pane = &g_mux.panes[g_mux.active_pane];
                if (pane->screen.win32_input_mode) {
                    char seq[64];
                    int sl = snprintf(seq, sizeof(seq), "\x1b[%u;%u;%u;0;%lu;%u_",
                                      (unsigned int)ke->wVirtualKeyCode,
                                      (unsigned int)ke->wVirtualScanCode,
                                      (unsigned int)ke->uChar.UnicodeChar,
                                      (unsigned long)ke->dwControlKeyState,
                                      (unsigned int)ke->wRepeatCount);
                    write_to_pane(seq, sl);
                }
            }
        }
        return;
    }
    WORD vk = ke->wVirtualKeyCode; DWORD ctrl = ke->dwControlKeyState; WCHAR uc = ke->uChar.UnicodeChar;
    BOOL is_ctrl = (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0, is_alt = (ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0, is_shift = (ctrl & SHIFT_PRESSED) != 0;

    // If active pane is in exited_hold state (showing error), any key closes it
    if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].exited_hold) {
        int c = g_mux.active_pane;
        int n = find_next_active_pane(c);
        pane_mark_dead(c);
        close_pane(c);
        if (n >= 0 && g_mux.panes[n].active) switch_pane(n);
        else {
            int f = -1;
            for (int i = 0; i < g_mux.pane_count; i++) if (g_mux.panes[i].active) { f = i; break; }
            if (f >= 0) switch_pane(f); else g_mux.running = 0;
        }
        g_mux.needs_redraw = 1;
        return;
    }

    if (g_mux.prefix_mode) { handle_prefix(vk, ctrl); return; }
    if ((uc == 0x02) || (vk == 'B' && is_ctrl && !is_alt && !is_shift)) { g_mux.prefix_mode = 1; return; }

    // Settings Mode 1: Main settings menu
    if (g_mux.settings_mode == 1) {
        if (vk == VK_ESCAPE) {
            load_config(); // discard unsaved
            g_mux.settings_mode = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        // Ctrl+S: save configuration
        if ((vk == 'S' && is_ctrl) || (uc == 0x13)) {
            save_config();
            g_mux.settings_mode = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        // Ctrl+Up: select previous item
        if (vk == VK_UP && is_ctrl) {
            if (g_mux.settings_sel > 0) g_mux.settings_sel--;
            g_mux.needs_redraw = 1;
            return;
        }
        // Ctrl+Down: select next item
        if (vk == VK_DOWN && is_ctrl) {
            if (g_mux.settings_sel < g_chooser_item_count - 1) g_mux.settings_sel++;
            g_mux.needs_redraw = 1;
            return;
        }
        // Up (without Ctrl): move selected item position up
        if ((vk == VK_UP && !is_ctrl) || uc == 'u' || uc == 'U') {
            int i = g_mux.settings_sel;
            if (i > 0 && i < g_chooser_item_count) {
                ChooserItem tmp = g_chooser_items[i];
                g_chooser_items[i] = g_chooser_items[i - 1];
                g_chooser_items[i - 1] = tmp;
                g_mux.settings_sel = i - 1;
                g_mux.needs_redraw = 1;
            }
            return;
        }
        // Down (without Ctrl): move selected item position down
        if ((vk == VK_DOWN && !is_ctrl) || uc == 'd' || uc == 'D') {
            int i = g_mux.settings_sel;
            if (i >= 0 && i < g_chooser_item_count - 1) {
                ChooserItem tmp = g_chooser_items[i];
                g_chooser_items[i] = g_chooser_items[i + 1];
                g_chooser_items[i + 1] = tmp;
                g_mux.settings_sel = i + 1;
                g_mux.needs_redraw = 1;
            }
            return;
        }
        // Ctrl+D / Delete / x: delete selected item
        if ((vk == 'D' && is_ctrl) || (uc == 0x04) || vk == VK_DELETE || uc == 'x' || uc == 'X') {
            int i = g_mux.settings_sel;
            if (i >= 0 && i < g_chooser_item_count && g_chooser_item_count > 1) {
                for (int k = i; k < g_chooser_item_count - 1; k++) {
                    g_chooser_items[k] = g_chooser_items[k + 1];
                }
                g_chooser_item_count--;
                if (g_mux.settings_sel >= g_chooser_item_count && g_mux.settings_sel > 0)
                    g_mux.settings_sel = g_chooser_item_count - 1;
                g_mux.needs_redraw = 1;
            }
            return;
        }
        // Enter / e: edit selected item
        if (vk == VK_RETURN || uc == 'e' || uc == 'E') {
            int i = g_mux.settings_sel;
            if (i >= 0 && i < g_chooser_item_count) {
                g_mux.settings_mode = 2;
                g_mux.settings_edit_idx = i;
                g_mux.settings_edit_field = 0;
                strncpy(g_mux.settings_edit_name, g_chooser_items[i].name, sizeof(g_mux.settings_edit_name) - 1);
                g_mux.settings_edit_name_len = (int)strlen(g_mux.settings_edit_name);
                g_mux.settings_edit_name_pos = g_mux.settings_edit_name_len;
                strncpy(g_mux.settings_edit_cmd, g_chooser_items[i].cmd, sizeof(g_mux.settings_edit_cmd) - 1);
                g_mux.settings_edit_cmd_len = (int)strlen(g_mux.settings_edit_cmd);
                g_mux.settings_edit_cmd_pos = g_mux.settings_edit_cmd_len;
                g_mux.needs_redraw = 1;
            }
            return;
        }
        if (uc == 'a' || uc == 'A' || uc == '+') { // Add new item
            if (g_chooser_item_count < MAX_CHOOSER_ITEMS) {
                g_mux.settings_mode = 2;
                g_mux.settings_edit_idx = -1;
                g_mux.settings_edit_field = 0;
                g_mux.settings_edit_name[0] = 0;
                g_mux.settings_edit_name_len = 0;
                g_mux.settings_edit_name_pos = 0;
                g_mux.settings_edit_cmd[0] = 0;
                g_mux.settings_edit_cmd_len = 0;
                g_mux.settings_edit_cmd_pos = 0;
                g_mux.needs_redraw = 1;
            }
            return;
        }
        if (uc == 'p' || uc == 'P') { // Open presets menu
            g_mux.settings_mode = 3;
            g_mux.needs_redraw = 1;
            return;
        }
        return;
    }

    // Settings Mode 2: Edit/Add item dialog
    if (g_mux.settings_mode == 2) {
        if (vk == VK_ESCAPE) {
            g_mux.settings_mode = 1;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_TAB) {
            g_mux.settings_edit_field = (g_mux.settings_edit_field == 0) ? 1 : 0;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_RETURN) {
            if (g_mux.settings_edit_name_len > 0 && g_mux.settings_edit_cmd_len > 0) {
                if (g_mux.settings_edit_idx >= 0 && g_mux.settings_edit_idx < g_chooser_item_count) {
                    snprintf(g_chooser_items[g_mux.settings_edit_idx].name, sizeof(g_chooser_items[0].name), "%s", g_mux.settings_edit_name);
                    snprintf(g_chooser_items[g_mux.settings_edit_idx].cmd, sizeof(g_chooser_items[0].cmd), "%s", g_mux.settings_edit_cmd);
                } else if (g_chooser_item_count < MAX_CHOOSER_ITEMS) {
                    int idx = g_chooser_item_count++;
                    snprintf(g_chooser_items[idx].name, sizeof(g_chooser_items[0].name), "%s", g_mux.settings_edit_name);
                    snprintf(g_chooser_items[idx].cmd, sizeof(g_chooser_items[0].cmd), "%s", g_mux.settings_edit_cmd);
                    g_mux.settings_sel = idx;
                }
            }
            g_mux.settings_mode = 1;
            g_mux.needs_redraw = 1;
            return;
        }
        char *buf = (g_mux.settings_edit_field == 0) ? g_mux.settings_edit_name : g_mux.settings_edit_cmd;
        int *len = (g_mux.settings_edit_field == 0) ? &g_mux.settings_edit_name_len : &g_mux.settings_edit_cmd_len;
        int *pos = (g_mux.settings_edit_field == 0) ? &g_mux.settings_edit_name_pos : &g_mux.settings_edit_cmd_pos;
        int cap = (g_mux.settings_edit_field == 0) ? sizeof(g_mux.settings_edit_name) - 1 : sizeof(g_mux.settings_edit_cmd) - 1;

        if (vk == VK_LEFT) {
            *pos = utf8_prev_grapheme(buf, *pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_RIGHT) {
            *pos = utf8_next_grapheme(buf, *len, *pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_HOME) { *pos = 0; g_mux.needs_redraw = 1; return; }
        if (vk == VK_END) { *pos = *len; g_mux.needs_redraw = 1; return; }
        if (vk == VK_BACK) {
            buf_backspace(buf, len, pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_DELETE) {
            buf_delete(buf, len, pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (uc >= 0xD800 && uc <= 0xDBFF) {
            g_high_surrogate = uc;
            return;
        }
        if (uc) {
            char u8[8] = {0}; int u8_count = 0;
            if (uc >= 0xDC00 && uc <= 0xDFFF && g_high_surrogate) {
                unsigned int cp = 0x10000 + (((unsigned int)(g_high_surrogate & 0x3FF)) << 10) + (uc & 0x3FF);
                g_high_surrogate = 0;
                u8[0] = (char)(0xF0 | (cp >> 18));
                u8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                u8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                u8[3] = (char)(0x80 | (cp & 0x3F));
                u8_count = 4;
            } else if (uc >= 0x20 || uc == 0x200D || (uc >= 0xFE00 && uc <= 0xFE0F)) {
                g_high_surrogate = 0;
                if (uc < 0x80) { u8[0] = (char)uc; u8_count = 1; }
                else if (uc < 0x800) { u8[0] = (char)(0xC0 | (uc >> 6)); u8[1] = (char)(0x80 | (uc & 0x3F)); u8_count = 2; }
                else { u8[0] = (char)(0xE0 | (uc >> 12)); u8[1] = (char)(0x80 | ((uc >> 6) & 0x3F)); u8[2] = (char)(0x80 | (uc & 0x3F)); u8_count = 3; }
            }
            if (u8_count > 0) {
                buf_insert_utf8(buf, len, pos, cap, u8, u8_count);
                g_mux.needs_redraw = 1;
                return;
            }
        }
        return;
    }

    // Settings Mode 3: Presets menu
    if (g_mux.settings_mode == 3) {
        if (vk == VK_ESCAPE) {
            g_mux.settings_mode = 1;
            g_mux.needs_redraw = 1;
            return;
        }
        for (int i = 0; i < g_preset_count; i++) {
            char digit = (char)('1' + i);
            if (uc == digit || vk == ('1' + i) || (vk == (VK_NUMPAD1 + i))) {
                if (g_chooser_item_count < MAX_CHOOSER_ITEMS) {
                    int idx = g_chooser_item_count++;
                    strncpy(g_chooser_items[idx].name, g_presets[i].name, sizeof(g_chooser_items[0].name) - 1);
                    strncpy(g_chooser_items[idx].cmd, g_presets[i].cmd, sizeof(g_chooser_items[0].cmd) - 1);
                    g_mux.settings_sel = idx;
                }
                g_mux.settings_mode = 1;
                g_mux.needs_redraw = 1;
                return;
            }
        }
        return;
    }

    // v8.33: rename mode - type the new title, Enter confirms, Esc cancels
    if (g_mux.rename_mode) {
        if (vk == VK_ESCAPE) {
            g_mux.rename_mode = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_RETURN) {
            if (g_mux.ctx_pane >= 0 && g_mux.ctx_pane < g_mux.pane_count && g_mux.rename_len > 0) {
                g_mux.rename_buf[g_mux.rename_len] = 0;
                if (g_mux.rename_len > 31) g_mux.rename_len = 31;
                memcpy(g_mux.panes[g_mux.ctx_pane].title, g_mux.rename_buf, g_mux.rename_len);
                g_mux.panes[g_mux.ctx_pane].title[g_mux.rename_len] = 0;
                if (g_mux.ctx_pane == g_mux.active_pane) update_host_title();
            }
            g_mux.rename_mode = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_LEFT) {
            g_mux.rename_pos = utf8_prev_grapheme(g_mux.rename_buf, g_mux.rename_pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_RIGHT) {
            g_mux.rename_pos = utf8_next_grapheme(g_mux.rename_buf, g_mux.rename_len, g_mux.rename_pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_HOME) {
            g_mux.rename_pos = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_END) {
            g_mux.rename_pos = g_mux.rename_len;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_BACK) {
            buf_backspace(g_mux.rename_buf, &g_mux.rename_len, &g_mux.rename_pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_DELETE) {
            buf_delete(g_mux.rename_buf, &g_mux.rename_len, &g_mux.rename_pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (uc >= 0xD800 && uc <= 0xDBFF) {
            g_high_surrogate = uc;
            return;
        }
        if (uc) {
            char u8[8] = {0};
            int u8_count = 0;
            if (uc >= 0xDC00 && uc <= 0xDFFF && g_high_surrogate) {
                unsigned int cp = 0x10000 + (((unsigned int)(g_high_surrogate & 0x3FF)) << 10) + (uc & 0x3FF);
                g_high_surrogate = 0;
                u8[0] = (char)(0xF0 | (cp >> 18));
                u8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                u8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                u8[3] = (char)(0x80 | (cp & 0x3F));
                u8_count = 4;
            } else if (uc >= 0x20 || uc == 0x200D || (uc >= 0xFE00 && uc <= 0xFE0F)) {
                g_high_surrogate = 0;
                if (uc < 0x80) {
                    u8[0] = (char)uc;
                    u8_count = 1;
                } else if (uc < 0x800) {
                    u8[0] = (char)(0xC0 | (uc >> 6));
                    u8[1] = (char)(0x80 | (uc & 0x3F));
                    u8_count = 2;
                } else {
                    u8[0] = (char)(0xE0 | (uc >> 12));
                    u8[1] = (char)(0x80 | ((uc >> 6) & 0x3F));
                    u8[2] = (char)(0x80 | (uc & 0x3F));
                    u8_count = 3;
                }
            }
            if (u8_count > 0) {
                buf_insert_utf8(g_mux.rename_buf, &g_mux.rename_len, &g_mux.rename_pos, sizeof(g_mux.rename_buf) - 1, u8, u8_count);
                g_mux.needs_redraw = 1;
                return;
            }
        }
        return;   // ignore other keys while renaming
    }
    // custom command mode - type command line, Enter runs, Esc cancels
    if (g_mux.custom_cmd_mode) {
        if (vk == VK_ESCAPE) {
            g_mux.custom_cmd_mode = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_RETURN) {
            g_mux.custom_cmd_mode = 0;
            WCHAR wcmd[256] = {0};
            if (g_mux.custom_cmd_len > 0) {
                MultiByteToWideChar(CP_UTF8, 0, g_mux.custom_cmd_buf, -1, wcmd, 255);
            } else {
                wcscpy(wcmd, L"cmd.exe");
            }
            int ni = create_pane_shell(wcmd);
            if (ni >= 0) switch_pane(ni);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_LEFT) {
            g_mux.custom_cmd_pos = utf8_prev_grapheme(g_mux.custom_cmd_buf, g_mux.custom_cmd_pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_RIGHT) {
            g_mux.custom_cmd_pos = utf8_next_grapheme(g_mux.custom_cmd_buf, g_mux.custom_cmd_len, g_mux.custom_cmd_pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_HOME) {
            g_mux.custom_cmd_pos = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_END) {
            g_mux.custom_cmd_pos = g_mux.custom_cmd_len;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_BACK) {
            buf_backspace(g_mux.custom_cmd_buf, &g_mux.custom_cmd_len, &g_mux.custom_cmd_pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_DELETE) {
            buf_delete(g_mux.custom_cmd_buf, &g_mux.custom_cmd_len, &g_mux.custom_cmd_pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (uc >= 0xD800 && uc <= 0xDBFF) {
            g_high_surrogate = uc;
            return;
        }
        if (uc) {
            char u8[8] = {0};
            int u8_count = 0;
            if (uc >= 0xDC00 && uc <= 0xDFFF && g_high_surrogate) {
                unsigned int cp = 0x10000 + (((unsigned int)(g_high_surrogate & 0x3FF)) << 10) + (uc & 0x3FF);
                g_high_surrogate = 0;
                u8[0] = (char)(0xF0 | (cp >> 18));
                u8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                u8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                u8[3] = (char)(0x80 | (cp & 0x3F));
                u8_count = 4;
            } else if (uc >= 0x20 || uc == 0x200D || (uc >= 0xFE00 && uc <= 0xFE0F)) {
                g_high_surrogate = 0;
                if (uc < 0x80) {
                    u8[0] = (char)uc;
                    u8_count = 1;
                } else if (uc < 0x800) {
                    u8[0] = (char)(0xC0 | (uc >> 6));
                    u8[1] = (char)(0x80 | (uc & 0x3F));
                    u8_count = 2;
                } else {
                    u8[0] = (char)(0xE0 | (uc >> 12));
                    u8[1] = (char)(0x80 | ((uc >> 6) & 0x3F));
                    u8[2] = (char)(0x80 | (uc & 0x3F));
                    u8_count = 3;
                }
            }
            if (u8_count > 0) {
                buf_insert_utf8(g_mux.custom_cmd_buf, &g_mux.custom_cmd_len, &g_mux.custom_cmd_pos, sizeof(g_mux.custom_cmd_buf) - 1, u8, u8_count);
                g_mux.needs_redraw = 1;
                return;
            }
        }
        return;
    }
    // v8.33: context menu - 1 = color, 2 = rename, Esc cancels
    if (g_mux.ctx_mode == 1) {
        if (uc == '1' || vk == '1') { g_mux.ctx_mode = 2; g_mux.needs_redraw = 1; return; }
        if (uc == '2' || vk == '2') {
            g_mux.ctx_mode = 0;
            g_mux.rename_mode = 1;
            g_mux.rename_len = 0;
            g_mux.rename_buf[0] = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        g_mux.ctx_mode = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    // v8.33: color picker - 1..8 selects
    if (g_mux.ctx_mode == 2) {
        // v8.54: also match vk - with a Chinese IME active, uChar can be
        // empty/0 even for plain digit keys, making keyboard selection dead.
        if ((uc >= '1' && uc <= '8') || (vk >= '1' && vk <= '8')) {
            int ci = (uc >= '1' && uc <= '8') ? (uc - '0') : (vk - '0');
            if (g_mux.ctx_pane >= 0 && g_mux.ctx_pane < g_mux.pane_count)
                g_mux.panes[g_mux.ctx_pane].color = ci;
            g_mux.ctx_mode = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        g_mux.ctx_mode = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    // shell chooser - 1..N selects configured item, Esc cancels
    if (g_mux.chooser_mode) {
        if (vk == VK_ESCAPE) {
            g_mux.chooser_mode = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        int selected_idx = -1;
        for (int i = 0; i < g_chooser_item_count; i++) {
            char digit = (char)('1' + i);
            if (uc == digit || vk == ('1' + i) || (vk == (VK_NUMPAD1 + i))) {
                selected_idx = i;
                break;
            }
        }
        if (selected_idx >= 0) {
            g_mux.chooser_mode = 0;
            if (strcmp(g_chooser_items[selected_idx].cmd, ":custom") == 0) {
                g_mux.custom_cmd_mode = 1;
                g_mux.custom_cmd_len = 0;
                g_mux.custom_cmd_pos = 0;
                g_mux.custom_cmd_buf[0] = 0;
            } else {
                WCHAR wcmd[256] = {0};
                MultiByteToWideChar(CP_UTF8, 0, g_chooser_items[selected_idx].cmd, -1, wcmd, 255);
                int ni = create_pane_shell(wcmd);
                if (ni >= 0) switch_pane(ni);
            }
            g_mux.needs_redraw = 1;
            return;
        }
        g_mux.chooser_mode = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    // v8.19: help view - PgUp/PgDn scroll it, any other key closes it
    if (g_mux.help_mode) {
        if (vk == VK_PRIOR || vk == VK_NEXT) {
            g_mux.help_scroll += (vk == VK_PRIOR) ? -(g_mux.host_rows / 2) : (g_mux.host_rows / 2);
            g_mux.needs_redraw = 1;
            return;
        }
        g_mux.help_mode = 0;
        g_mux.help_scroll = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    if (g_mux.active_pane < 0 || g_mux.active_pane >= g_mux.pane_count) return;
    Pane *pane = &g_mux.panes[g_mux.active_pane]; if (!pane->active) return;
    if (pane->scroll_offset > 0 && !pane->screen.in_alt_screen && vk != VK_PRIOR && vk != VK_NEXT) { pane->scroll_offset = 0; g_mux.needs_redraw = 1; }
    ScreenBuffer *scr = &pane->screen;

    if (vk == VK_BACK) {
        int del_wchars = get_prev_grapheme_wchars(pane->input_history, pane->input_history_len, pane->input_history_pos);
        if (del_wchars < 1) del_wchars = 1;
        if (pane->input_history_pos >= del_wchars) {
            if (pane->input_history_pos < pane->input_history_len) {
                memmove(pane->input_history + pane->input_history_pos - del_wchars,
                        pane->input_history + pane->input_history_pos,
                        (pane->input_history_len - pane->input_history_pos) * sizeof(WCHAR));
            }
            pane->input_history_len -= del_wchars;
            pane->input_history_pos -= del_wchars;
        } else {
            pane->input_history_len = 0;
            pane->input_history_pos = 0;
        }

        if (scr->win32_input_mode) {
            for (int b = 0; b < del_wchars; b++) {
                char seq_d[64], seq_u[64];
                int sld = snprintf(seq_d, sizeof(seq_d), "\x1b[8;14;8;1;%lu;1_", (unsigned long)ke->dwControlKeyState);
                write_to_pane(seq_d, sld);
                int slu = snprintf(seq_u, sizeof(seq_u), "\x1b[8;14;8;0;%lu;1_", (unsigned long)ke->dwControlKeyState);
                write_to_pane(seq_u, slu);
            }
            return;
        } else {
            for (int b = 0; b < del_wchars; b++) {
                char c = is_ctrl ? 0x08 : 0x7F;
                write_to_pane(&c, 1);
            }
            return;
        }
    }

    if (vk == VK_DELETE) {
        int del_wchars = get_next_grapheme_wchars(pane->input_history, pane->input_history_len, pane->input_history_pos);
        if (del_wchars < 1) del_wchars = 1;
        if (pane->input_history_pos + del_wchars <= pane->input_history_len) {
            if (pane->input_history_pos + del_wchars < pane->input_history_len) {
                memmove(pane->input_history + pane->input_history_pos,
                        pane->input_history + pane->input_history_pos + del_wchars,
                        (pane->input_history_len - pane->input_history_pos - del_wchars) * sizeof(WCHAR));
            }
            pane->input_history_len -= del_wchars;
        } else {
            pane->input_history_len = 0;
        }

        if (scr->win32_input_mode) {
            for (int b = 0; b < del_wchars; b++) {
                char seq_d[64], seq_u[64];
                int sld = snprintf(seq_d, sizeof(seq_d), "\x1b[46;83;0;1;%lu;1_", (unsigned long)ke->dwControlKeyState);
                write_to_pane(seq_d, sld);
                int slu = snprintf(seq_u, sizeof(seq_u), "\x1b[46;83;0;0;%lu;1_", (unsigned long)ke->dwControlKeyState);
                write_to_pane(seq_u, slu);
            }
            return;
        } else {
            for (int b = 0; b < del_wchars; b++) {
                const char *s = "\x1b[3~";
                write_to_pane(s, (int)strlen(s));
            }
            return;
        }
    }

    if (vk == VK_LEFT) {
        int steps = get_prev_grapheme_wchars(pane->input_history, pane->input_history_len, pane->input_history_pos);
        if (steps < 1) steps = 1;
        pane->input_history_pos -= steps;
        if (pane->input_history_pos < 0) pane->input_history_pos = 0;

        if (scr->win32_input_mode) {
            for (int b = 0; b < steps; b++) {
                char seq_d[64], seq_u[64];
                int sld = snprintf(seq_d, sizeof(seq_d), "\x1b[37;75;0;1;%lu;1_", (unsigned long)ke->dwControlKeyState);
                write_to_pane(seq_d, sld);
                int slu = snprintf(seq_u, sizeof(seq_u), "\x1b[37;75;0;0;%lu;1_", (unsigned long)ke->dwControlKeyState);
                write_to_pane(seq_u, slu);
            }
            return;
        } else {
            for (int b = 0; b < steps; b++) {
                const char *s = scr->app_cursor_keys ? "\x1bOD" : "\x1b[D";
                write_to_pane(s, (int)strlen(s));
            }
            return;
        }
    }

    if (vk == VK_RIGHT) {
        int steps = get_next_grapheme_wchars(pane->input_history, pane->input_history_len, pane->input_history_pos);
        if (steps < 1) steps = 1;
        pane->input_history_pos += steps;
        if (pane->input_history_pos > pane->input_history_len) pane->input_history_pos = pane->input_history_len;

        if (scr->win32_input_mode) {
            for (int b = 0; b < steps; b++) {
                char seq_d[64], seq_u[64];
                int sld = snprintf(seq_d, sizeof(seq_d), "\x1b[39;77;0;1;%lu;1_", (unsigned long)ke->dwControlKeyState);
                write_to_pane(seq_d, sld);
                int slu = snprintf(seq_u, sizeof(seq_u), "\x1b[39;77;0;0;%lu;1_", (unsigned long)ke->dwControlKeyState);
                write_to_pane(seq_u, slu);
            }
            return;
        } else {
            for (int b = 0; b < steps; b++) {
                const char *s = scr->app_cursor_keys ? "\x1bOC" : "\x1b[C";
                write_to_pane(s, (int)strlen(s));
            }
            return;
        }
    }

    if (vk == VK_HOME) {
        pane->input_history_pos = 0;
    } else if (vk == VK_END) {
        pane->input_history_pos = pane->input_history_len;
    } else if (vk == VK_UP || vk == VK_DOWN || vk == VK_RETURN || vk == VK_ESCAPE) {
        pane->input_history_len = 0;
        pane->input_history_pos = 0;
    } else if ((uc >= 0x20 || uc == 0x200D || (uc >= 0xFE00 && uc <= 0xFE0F) || (uc >= 0xD800 && uc <= 0xDFFF)) && !is_ctrl && !is_alt) {
        if (pane->input_history_len < 255) {
            if (pane->input_history_pos < pane->input_history_len) {
                memmove(pane->input_history + pane->input_history_pos + 1,
                        pane->input_history + pane->input_history_pos,
                        (pane->input_history_len - pane->input_history_pos) * sizeof(WCHAR));
            }
            pane->input_history[pane->input_history_pos++] = uc;
            pane->input_history_len++;
        }
    }

    if (scr->win32_input_mode) {
        char win32_seq[64];
        int win32_sl = snprintf(win32_seq, sizeof(win32_seq), "\x1b[%u;%u;%u;1;%lu;%u_",
                                (unsigned int)ke->wVirtualKeyCode,
                                (unsigned int)ke->wVirtualScanCode,
                                (unsigned int)ke->uChar.UnicodeChar,
                                (unsigned long)ke->dwControlKeyState,
                                (unsigned int)ke->wRepeatCount);
        write_to_pane(win32_seq, win32_sl);
        return;
    }

    char seq[32]; int sl = 0;
    switch (vk) {
        case VK_UP: sl = snprintf(seq, sizeof(seq), scr->app_cursor_keys ? "\x1bOA" : "\x1b[A"); break;
        case VK_DOWN: sl = snprintf(seq, sizeof(seq), scr->app_cursor_keys ? "\x1bOB" : "\x1b[B"); break;
        case VK_RIGHT: sl = snprintf(seq, sizeof(seq), scr->app_cursor_keys ? "\x1bOC" : "\x1b[C"); break;
        case VK_LEFT: sl = snprintf(seq, sizeof(seq), scr->app_cursor_keys ? "\x1bOD" : "\x1b[D"); break;
        case VK_HOME: sl = snprintf(seq, sizeof(seq), is_ctrl ? "\x1b[1;5H" : "\x1b[H"); break;
        case VK_END: sl = snprintf(seq, sizeof(seq), is_ctrl ? "\x1b[1;5F" : "\x1b[F"); break;
        case VK_INSERT: sl = snprintf(seq, sizeof(seq), "\x1b[2~"); break;
        case VK_DELETE: sl = snprintf(seq, sizeof(seq), "\x1b[3~"); break;
        case VK_PRIOR: sl = snprintf(seq, sizeof(seq), "\x1b[5~"); break;
        case VK_NEXT: sl = snprintf(seq, sizeof(seq), "\x1b[6~"); break;
        case VK_F1: sl = snprintf(seq, sizeof(seq), "\x1bOP"); break;
        case VK_F2: sl = snprintf(seq, sizeof(seq), "\x1bOQ"); break;
        case VK_F3: sl = snprintf(seq, sizeof(seq), "\x1bOR"); break;
        case VK_F4: sl = snprintf(seq, sizeof(seq), "\x1bOS"); break;
        case VK_F5: sl = snprintf(seq, sizeof(seq), "\x1b[15~"); break;
        case VK_F6: sl = snprintf(seq, sizeof(seq), "\x1b[17~"); break;
        case VK_F7: sl = snprintf(seq, sizeof(seq), "\x1b[18~"); break;
        case VK_F8: sl = snprintf(seq, sizeof(seq), "\x1b[19~"); break;
        case VK_F9: sl = snprintf(seq, sizeof(seq), "\x1b[20~"); break;
        case VK_F10: sl = snprintf(seq, sizeof(seq), "\x1b[21~"); break;
        case VK_F11: sl = snprintf(seq, sizeof(seq), "\x1b[23~"); break;
        case VK_F12: sl = snprintf(seq, sizeof(seq), "\x1b[24~"); break;
        case VK_TAB: if (is_shift) sl = snprintf(seq, sizeof(seq), "\x1b[Z"); else { seq[0] = '\t'; sl = 1; } break;
        case VK_ESCAPE: seq[0] = 0x1B; sl = 1; break;
        case VK_RETURN: seq[0] = is_ctrl ? 0x0A : 0x0D; sl = 1; break;   // v7: Ctrl+Enter = LF
        case VK_SPACE: seq[0] = is_ctrl ? 0 : ' '; sl = 1; break;
        default:
            if (uc >= 0xD800 && uc <= 0xDBFF) {
                g_high_surrogate = uc;
                return;
            }
            if (uc) {
                if (is_alt) seq[sl++] = 0x1B;
                if (uc >= 0xDC00 && uc <= 0xDFFF && g_high_surrogate) {
                    unsigned int cp = 0x10000 + (((unsigned int)(g_high_surrogate & 0x3FF)) << 10) + (uc & 0x3FF);
                    g_high_surrogate = 0;
                    seq[sl++] = (char)(0xF0 | (cp >> 18));
                    seq[sl++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                    seq[sl++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    seq[sl++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    g_high_surrogate = 0;
                    if (uc < 0x80) seq[sl++] = (char)uc;
                    else if (uc < 0x800) { seq[sl++] = 0xC0 | (uc >> 6); seq[sl++] = 0x80 | (uc & 0x3F); }
                    else { seq[sl++] = 0xE0 | (uc >> 12); seq[sl++] = 0x80 | ((uc >> 6) & 0x3F); seq[sl++] = 0x80 | (uc & 0x3F); }
                }
            }
            break;
    }
    if (sl > 0) for (WORD r = 0; r < ke->wRepeatCount; r++) write_to_pane(seq, sl);
}

static void handle_mouse(MOUSE_EVENT_RECORD *me) {
    int mx = me->dwMousePosition.X, my = me->dwMousePosition.Y;
    // v8.54: log EVERY mouse event up front (throttled for moves) so any
    // interaction - including ones that don't hit a UI path - is traceable.
    log_mouse_event("ev", me);
    // v8.11/v8.26: track mouse position. Redraw whenever the mouse enters OR
    // leaves the tab bar row, so hover highlights appear when over a button and
    // clear as soon as the pointer moves away (previously they stuck).
    if (mx != g_mouse_x || my != g_mouse_y) {
        int prev_in = g_mouse_prev_in_tabbar;
        g_mouse_x = mx; g_mouse_y = my;
        int now_in = (my == 0);
        // v8.35: redraw on EVERY move while on the tab bar (hover highlight
        // follows the pointer cell-by-cell), plus once when leaving it (so the
        // highlight clears). Moving elsewhere doesn't redraw.
        // v8.52: while ANY popup is open, redraw on every move too - the color
        // picker's swatches need live hover highlights (previously the picker
        // was never redrawn on mouse move, so hovering did nothing).
        if (now_in || (prev_in && !now_in) ||
            g_mux.chooser_mode || g_mux.ctx_mode || g_mux.rename_mode || g_mux.custom_cmd_mode || g_mux.settings_mode)
            g_mux.needs_redraw = 1;
        g_mouse_prev_in_tabbar = now_in;

        // When mouse transitions into outer tab bar (!prev_in && now_in), notify
        // child pane ONCE with a safe non-tabbar coordinate (y = 2, i.e. child row 1)
        // so nested child termux or app immediately clears its tab hover highlight!
        if (!prev_in && now_in) {
            if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active) {
                ScreenBuffer *s = &g_mux.panes[g_mux.active_pane].screen;
                if (s->mouse_tracking) {
                    int x = mx + 1;
                    char seq[64]; int len = 0;
                    if (s->mouse_sgr) {
                        len = snprintf(seq, sizeof(seq), "\x1b[<35;%d;2M", x);
                    } else if (x <= 223) {
                        seq[0] = '\x1b'; seq[1] = '['; seq[2] = 'M';
                        seq[3] = 32 + 35; seq[4] = 32 + x; seq[5] = 32 + 2;
                        len = 6;
                    }
                    if (len > 0) write_to_pane(seq, len);
                }
            }
        }
    }
    if (my == 0) {   // v8.22: tab bar at top
        // v8.48: if a popup is already open, clicking the tab bar (any button)
        // only closes it - never opens another popup on top (mutual exclusion).
        int press2 = (me->dwButtonState & (FROM_LEFT_1ST_BUTTON_PRESSED | FROM_LEFT_2ND_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED)) != 0;
        if (press2 && (me->dwEventFlags == 0 || me->dwEventFlags == DOUBLE_CLICK) &&
            (g_mux.chooser_mode || g_mux.ctx_mode || g_mux.rename_mode || g_mux.custom_cmd_mode || g_mux.settings_mode)) {
            g_mux.chooser_mode = 0;
            g_mux.ctx_mode = 0;
            g_mux.rename_mode = 0;
            g_mux.custom_cmd_mode = 0;
            if (g_mux.settings_mode) {
                load_config();
                g_mux.settings_mode = 0;
            }
            g_mux.needs_redraw = 1;
            return;
        }
        // v8.30: handle a click on a NEW press/double-click only (flags==0 or
        // DOUBLE_CLICK). Holding the button and dragging generates MOUSE_MOVED
        // events with the button still down - those must NOT re-trigger (or, in
        // the chooser, cancel) anything, otherwise a held click makes the [+]
        // chooser flash open-and-closed instantly.
        int press = (me->dwButtonState & (FROM_LEFT_1ST_BUTTON_PRESSED | FROM_LEFT_2ND_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED)) != 0;
        if (press && (me->dwEventFlags == 0 || me->dwEventFlags == DOUBLE_CLICK)) {
            int mbtn = 0;   // 0 = left, 1 = middle, 2 = right
            // v8.53: LEFT > RIGHT > MIDDLE priority. The user's mouse has a
            // physically stuck middle button (btn always contains 0x4), so a
            // right click arrives as 0x6 (right+middle) and a left click as
            // 0x5. The old MIDDLE-first order turned right-click into the
            // middle-click "close tab" action - the context menu was
            // unreachable. With LEFT/RIGHT first, 0x5 stays a left click,
            // 0x6 stays a right click, and only a PURE middle click (0x4)
            // closes the tab.
            if (me->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) mbtn = 0;
            else if (me->dwButtonState & RIGHTMOST_BUTTON_PRESSED) mbtn = 2;
            else if (me->dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) mbtn = 1;
            for (int i = 0; i < g_mux.tab_count; i++) {
                PaneTabInfo *t = &g_mux.tab_info[i];
                if (mx < t->start_col || mx >= t->end_col) continue;
                // v8.27: middle click on a real tab closes it (browser-style)
                if (mbtn == 1 && t->pane_idx >= 0) {
                    int ci = t->pane_idx;
                    int was_active = (ci == g_mux.active_pane);
                    close_pane(ci);
                    if (was_active) {
                        int n = find_next_active_pane(ci);
                        if (n >= 0) { g_mux.active_pane = n; g_mux.panes[n].scroll_offset = 0; }
                        else {
                            int f = -1;
                            for (int k = 0; k < g_mux.pane_count; k++) if (g_mux.panes[k].active) { f = k; break; }
                            g_mux.active_pane = f;
                            if (f < 0) { g_mux.running = 0; return; }
                            g_mux.panes[f].scroll_offset = 0;
                        }
                    }
                    g_mux.needs_redraw = 1;
                    return;
                }
                if (t->pane_idx == -2) {
                    // v8.18/8.19: termux brand toggles the help view (any button)
                    g_mux.help_mode = !g_mux.help_mode;
                    if (!g_mux.help_mode) g_mux.help_scroll = 0;
                    g_mux.needs_redraw = 1;
                    return;
                }
                if (t->pane_idx == -1) {
                    // v8.21: [+] opens the shell chooser
                    g_mux.ctx_mode = 0;
                    g_mux.rename_mode = 0;
                    g_mux.custom_cmd_mode = 0;
                    g_mux.settings_mode = 0;
                    g_mux.chooser_mode = 1;
                    g_pop_anchor_x = mx;
                    g_mux.needs_redraw = 1;
                    return;
                }
                if (t->pane_idx == -3) {
                    // [⚙] opens graphical settings UI
                    g_mux.chooser_mode = 0;
                    g_mux.ctx_mode = 0;
                    g_mux.rename_mode = 0;
                    g_mux.custom_cmd_mode = 0;
                    g_mux.help_mode = 0;
                    g_mux.settings_mode = (g_mux.settings_mode ? 0 : 1);
                    g_mux.settings_sel = 0;
                    g_pop_anchor_x = mx;
                    g_mux.needs_redraw = 1;
                    return;
                }
                // v8.33: right click on a real tab opens the context menu
                if (mbtn == 2) {
                    if (t->pane_idx >= 0 && t->pane_idx < g_mux.pane_count && g_mux.panes[t->pane_idx].active) {
                        // v8.46: close any other popup first (mutual exclusion)
                        g_mux.chooser_mode = 0;
                        g_mux.rename_mode = 0;
                        g_mux.custom_cmd_mode = 0;
                        g_mux.settings_mode = 0;
                        g_mux.ctx_mode = 1;
                        g_mux.ctx_pane = t->pane_idx;
                        g_pop_anchor_x = mx;   // v8.45: lock position
                        g_mux.needs_redraw = 1;
                    }
                    return;
                }
                if (mbtn != 0) return;   // middle already handled above; other: ignore
                if (!g_mux.panes[t->pane_idx].active) continue;
                if (mx >= t->close_start && mx < t->close_end) {
                    // v8.10: 'x' - close this tab
                    int ci = t->pane_idx;
                    int was_active = (ci == g_mux.active_pane);
                    close_pane(ci);
                    if (was_active) {
                        int n = find_next_active_pane(ci);
                        if (n >= 0) { g_mux.active_pane = n; g_mux.panes[n].scroll_offset = 0; }
                        else {
                            int f = -1;
                            for (int k = 0; k < g_mux.pane_count; k++) if (g_mux.panes[k].active) { f = k; break; }
                            g_mux.active_pane = f;
                            if (f < 0) { g_mux.running = 0; return; }
                            g_mux.panes[f].scroll_offset = 0;
                        }
                    }
                    g_mux.needs_redraw = 1;
                    return;
                }
                // tab body - switch to it (leaves help view)
                g_mux.help_mode = 0;
                switch_pane(t->pane_idx);
                return;
            }
            // v8.57: tab-bar BACKGROUND right-click no longer opens the menu
            // (v8.53 fallback removed - it fired for right-clicks on empty tab
            // bar space, which the user reported as "menu pops up without
            // right-clicking a tab"). Menu opens ONLY on right-click of a tab.
            return;
        }
        return;
    }
    // v8.52: popup (chooser / ctx menu / color picker) click handling, at the
    // SAME level for all popups. v8.33 nested the ctx menu handling inside
    // `if (g_mux.chooser_mode)` - but opening the menu sets ctx_mode with
    // chooser_mode == 0, so EVERY mouse click in the menu / color picker fell
    // through to the pane and did nothing (swatch 8 was unreachable by mouse).
    // CRITICAL: the renderer positions with CSI (1-based rows/cols), but the
    // mouse event coords are 0-based. Convert to 1-based before comparing so
    // the click target matches the drawn row exactly (previously it was off by
    // one row - clicks hit one row below the drawn option).
    int popup_open = g_mux.chooser_mode || g_mux.ctx_mode || g_mux.settings_mode;
    if (popup_open) {
        // v8.30: only a NEW press/double-click acts on a popup.
        int pbtn = (me->dwButtonState & (FROM_LEFT_1ST_BUTTON_PRESSED | FROM_LEFT_2ND_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED)) != 0;
        if (pbtn && (me->dwEventFlags == 0 || me->dwEventFlags == DOUBLE_CLICK)) {
            if (g_mux.settings_mode) {
                if (g_mux.settings_mode == 1) {
                    int top, left, sw, sh;
                    settings_geom(g_mux.host_rows, g_mux.host_cols, &top, &left, &sw, &sh);
                    int r = my + 1, c = mx + 1;
                    int in_box = (r >= top && r < top + sh && c >= left && c < left + sw);
                    if (in_box) {
                        for (int i = 0; i < g_chooser_item_count; i++) {
                            if (r == top + 4 + i) {
                                g_mux.settings_sel = i;
                                // [↑]
                                if (c >= left + 46 && c <= left + 48) {
                                    if (i > 0) {
                                        ChooserItem tmp = g_chooser_items[i];
                                        g_chooser_items[i] = g_chooser_items[i - 1];
                                        g_chooser_items[i - 1] = tmp;
                                        g_mux.settings_sel = i - 1;
                                    }
                                }
                                // [↓]
                                else if (c >= left + 49 && c <= left + 51) {
                                    if (i < g_chooser_item_count - 1) {
                                        ChooserItem tmp = g_chooser_items[i];
                                        g_chooser_items[i] = g_chooser_items[i + 1];
                                        g_chooser_items[i + 1] = tmp;
                                        g_mux.settings_sel = i + 1;
                                    }
                                }
                                // [改]
                                else if (c >= left + 53 && c <= left + 56) {
                                    g_mux.settings_mode = 2;
                                    g_mux.settings_edit_idx = i;
                                    g_mux.settings_edit_field = 0;
                                    strncpy(g_mux.settings_edit_name, g_chooser_items[i].name, sizeof(g_mux.settings_edit_name) - 1);
                                    g_mux.settings_edit_name_len = (int)strlen(g_mux.settings_edit_name);
                                    g_mux.settings_edit_name_pos = g_mux.settings_edit_name_len;
                                    strncpy(g_mux.settings_edit_cmd, g_chooser_items[i].cmd, sizeof(g_mux.settings_edit_cmd) - 1);
                                    g_mux.settings_edit_cmd_len = (int)strlen(g_mux.settings_edit_cmd);
                                    g_mux.settings_edit_cmd_pos = g_mux.settings_edit_cmd_len;
                                }
                                // [删]
                                else if (c >= left + 58 && c <= left + 61) {
                                    if (g_chooser_item_count > 1) {
                                        for (int k = i; k < g_chooser_item_count - 1; k++) {
                                            g_chooser_items[k] = g_chooser_items[k + 1];
                                        }
                                        g_chooser_item_count--;
                                        if (g_mux.settings_sel >= g_chooser_item_count && g_mux.settings_sel > 0)
                                            g_mux.settings_sel = g_chooser_item_count - 1;
                                    }
                                }
                                // Click text / row -> select item (double click -> edit)
                                else if (me->dwEventFlags == DOUBLE_CLICK) {
                                    g_mux.settings_mode = 2;
                                    g_mux.settings_edit_idx = i;
                                    g_mux.settings_edit_field = 0;
                                    strncpy(g_mux.settings_edit_name, g_chooser_items[i].name, sizeof(g_mux.settings_edit_name) - 1);
                                    g_mux.settings_edit_name_len = (int)strlen(g_mux.settings_edit_name);
                                    g_mux.settings_edit_name_pos = g_mux.settings_edit_name_len;
                                    strncpy(g_mux.settings_edit_cmd, g_chooser_items[i].cmd, sizeof(g_mux.settings_edit_cmd) - 1);
                                    g_mux.settings_edit_cmd_len = (int)strlen(g_mux.settings_edit_cmd);
                                    g_mux.settings_edit_cmd_pos = g_mux.settings_edit_cmd_len;
                                }
                                g_mux.needs_redraw = 1;
                                return;
                            }
                        }
                        // Click [+] 添加新条目
                        if (r == top + 4 + g_chooser_item_count) {
                            if (c >= left + 2 && c <= left + 20) {
                                if (g_chooser_item_count < MAX_CHOOSER_ITEMS) {
                                    g_mux.settings_mode = 2;
                                    g_mux.settings_edit_idx = -1;
                                    g_mux.settings_edit_field = 0;
                                    g_mux.settings_edit_name[0] = 0;
                                    g_mux.settings_edit_name_len = 0;
                                    g_mux.settings_edit_name_pos = 0;
                                    g_mux.settings_edit_cmd[0] = 0;
                                    g_mux.settings_edit_cmd_len = 0;
                                    g_mux.settings_edit_cmd_pos = 0;
                                    g_mux.needs_redraw = 1;
                                    return;
                                }
                            }
                            if (c >= left + 22 && c <= left + 45) {
                                g_mux.settings_mode = 3;
                                g_mux.needs_redraw = 1;
                                return;
                            }
                        }
                        // Click [Ctrl+S] 保存配置
                        if (r == top + 6 + g_chooser_item_count) {
                            if (c >= left + 2 && c <= left + 22) {
                                save_config();
                                g_mux.settings_mode = 0;
                                g_mux.needs_redraw = 1;
                                return;
                            }
                            if (c >= left + 31 && c <= left + sw - 2) {
                                load_config();
                                g_mux.settings_mode = 0;
                                g_mux.needs_redraw = 1;
                                return;
                            }
                        }
                    }
                    load_config();
                    g_mux.settings_mode = 0;
                    g_mux.needs_redraw = 1;
                    return;
                }
                if (g_mux.settings_mode == 2) {
                    int top = 3;
                    int ew = EDIT_BOX_W;
                    if (ew > g_mux.host_cols) ew = g_mux.host_cols;
                    int left = (g_mux.host_cols - ew) / 2;
                    if (left < 0) left = 0;
                    int r = my + 1, c = mx + 1;
                    if (r == top + 1) { g_mux.settings_edit_field = 0; g_mux.needs_redraw = 1; return; }
                    if (r == top + 2) { g_mux.settings_edit_field = 1; g_mux.needs_redraw = 1; return; }
                    if (r == top + 4) {
                        if (c >= left + 2 && c <= left + 20) {
                            if (g_mux.settings_edit_name_len > 0 && g_mux.settings_edit_cmd_len > 0) {
                                if (g_mux.settings_edit_idx >= 0 && g_mux.settings_edit_idx < g_chooser_item_count) {
                                    snprintf(g_chooser_items[g_mux.settings_edit_idx].name, sizeof(g_chooser_items[0].name), "%s", g_mux.settings_edit_name);
                                    snprintf(g_chooser_items[g_mux.settings_edit_idx].cmd, sizeof(g_chooser_items[0].cmd), "%s", g_mux.settings_edit_cmd);
                                } else if (g_chooser_item_count < MAX_CHOOSER_ITEMS) {
                                    int idx = g_chooser_item_count++;
                                    snprintf(g_chooser_items[idx].name, sizeof(g_chooser_items[0].name), "%s", g_mux.settings_edit_name);
                                    snprintf(g_chooser_items[idx].cmd, sizeof(g_chooser_items[0].cmd), "%s", g_mux.settings_edit_cmd);
                                    g_mux.settings_sel = idx;
                                }
                            }
                            g_mux.settings_mode = 1;
                            g_mux.needs_redraw = 1;
                            return;
                        }
                        if (c >= left + 30 && c <= left + ew - 2) {
                            g_mux.settings_mode = 1;
                            g_mux.needs_redraw = 1;
                            return;
                        }
                    }
                    g_mux.settings_mode = 1;
                    g_mux.needs_redraw = 1;
                    return;
                }
                if (g_mux.settings_mode == 3) {
                    int top = 3;
                    int pw = PRESET_BOX_W;
                    if (pw > g_mux.host_cols) pw = g_mux.host_cols;
                    int left = (g_mux.host_cols - pw) / 2;
                    if (left < 0) left = 0;
                    int r = my + 1;
                    for (int i = 0; i < g_preset_count; i++) {
                        if (r == top + 1 + i) {
                            if (g_chooser_item_count < MAX_CHOOSER_ITEMS) {
                                int idx = g_chooser_item_count++;
                                strncpy(g_chooser_items[idx].name, g_presets[i].name, sizeof(g_chooser_items[0].name) - 1);
                                strncpy(g_chooser_items[idx].cmd, g_presets[i].cmd, sizeof(g_chooser_items[0].cmd) - 1);
                                g_mux.settings_sel = idx;
                            }
                            g_mux.settings_mode = 1;
                            g_mux.needs_redraw = 1;
                            return;
                        }
                    }
                    g_mux.settings_mode = 1;
                    g_mux.needs_redraw = 1;
                    return;
                }
            }
            if (g_mux.ctx_mode) {
                // v8.50: compute top/left for BOTH menu and color picker so the
                // hit-testing matches where they're actually drawn.
                int top = 2, left = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
                if (left + CTX_W > g_mux.host_cols) left = (g_pop_anchor_x >= 0 ? g_pop_anchor_x : g_mouse_x) - CTX_W;
                if (left < 0) left = 0;
                int r = my + 1, c = mx + 1;
                if (g_mux.ctx_mode == 1) {
                    if (r == top + 1 && c >= left && c < left + CTX_W) {   // [1] 改颜色
                        g_mux.ctx_mode = 2;
                        g_mux.needs_redraw = 1;
                        return;
                    }
                    if (r == top + 2 && c >= left && c < left + CTX_W) {   // [2] 改标题
                        g_mux.ctx_mode = 0;
                        g_mux.rename_mode = 1;
                        g_mux.rename_len = 0;
                        g_mux.rename_pos = 0;
                        g_mux.rename_buf[0] = 0;
                        g_mux.needs_redraw = 1;
                        return;
                    }
                } else {
                    // v8.49: precise swatch hit-testing. Each row draws 4
                    // swatches; row1 = colors 1-4, row2 = 5-8.
                    // v8.51: swatch k (0-based) starts at 1-based col left+2+4k.
                    // dc = c - (left+2); swatch = dc/4 (0..3).
                    int swatch = -1;
                    if (r == top + 1 || r == top + 2) {
                        int base = (r == top + 1) ? 1 : 5;
                        int dc = c - (left + 2);
                        if (dc >= 0) {
                            int which = dc / 4;
                            if (which >= 0 && which < 4) swatch = base + which;
                        }
                    }
                    if (swatch >= 1 && swatch <= 8) {
                        if (g_mux.ctx_pane >= 0 && g_mux.ctx_pane < g_mux.pane_count) {
                            g_mux.panes[g_mux.ctx_pane].color = swatch;
                        }
                        g_mux.ctx_mode = 0;
                        g_mux.needs_redraw = 1;
                        return;
                    }
                }
                g_mux.ctx_mode = 0;
                g_mux.needs_redraw = 1;
                return;
            }
            // chooser: 1..N items from termux.ini, click anywhere else cancels
            int top, left, cw, ch;
            chooser_geom(g_mux.host_rows, g_mux.host_cols, &top, &left, &cw, &ch);
            int r = my + 1, c = mx + 1;   // convert to 1-based (as the renderer)
            int in_box = (r >= top && r < top + ch && c >= left && c < left + cw);
            if (in_box) {
                for (int i = 0; i < g_chooser_item_count; i++) {
                    if (r == top + 1 + i) {
                        g_mux.chooser_mode = 0;
                        if (strcmp(g_chooser_items[i].cmd, ":custom") == 0) {
                            g_mux.custom_cmd_mode = 1;
                            g_mux.custom_cmd_len = 0;
                            g_mux.custom_cmd_pos = 0;
                            g_mux.custom_cmd_buf[0] = 0;
                        } else {
                            WCHAR wcmd[256] = {0};
                            MultiByteToWideChar(CP_UTF8, 0, g_chooser_items[i].cmd, -1, wcmd, 255);
                            int ni = create_pane_shell(wcmd);
                            if (ni >= 0) switch_pane(ni);
                        }
                        g_mux.needs_redraw = 1;
                        return;
                    }
                }
            }
            // click anywhere else cancels
            g_mux.chooser_mode = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        // swallow everything else while a popup is open (moves, wheel, releases)
        return;
    }
    // v8.52: rename box & custom cmd box are keyboard-driven (Esc/Enter); swallow clicks
    if (g_mux.rename_mode || g_mux.custom_cmd_mode) {
        return;
    }
    // v8.19: in help view, the mouse wheel scrolls the help content
    if (g_mux.help_mode) {
        if (me->dwEventFlags == MOUSE_WHEELED) {
            int d = (short)HIWORD(me->dwButtonState);
            g_mux.help_scroll += (d > 0 ? -3 : 3);
            g_mux.needs_redraw = 1;
        }
        return;
    }
    if (g_mux.active_pane < 0) return;
    Pane *p = &g_mux.panes[g_mux.active_pane];
    if (!p->active) return;
    ScreenBuffer *s = &p->screen;
    if (me->dwButtonState & (FROM_LEFT_1ST_BUTTON_PRESSED | FROM_LEFT_2ND_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED)) {
        p->input_history_len = 0;
        p->input_history_pos = 0;
    }

    if (me->dwEventFlags == MOUSE_WHEELED) {
        int d = (short)HIWORD(me->dwButtonState);
        if (s->mouse_tracking) {
            int x = mx + 1, y = my;
            if (x < 1) x = 1;
            if (x > s->cols) x = s->cols;
            if (y < 1) y = 1;
            if (y > s->rows) y = s->rows;
            char seq[64]; int len = 0;
            if (s->mouse_sgr) {
                int btn = d > 0 ? 64 : 65;
                len = snprintf(seq, sizeof(seq), "\x1b[<%d;%d;%dM", btn, x, y);
            } else if (x <= 223 && y <= 223) {
                seq[0] = '\x1b'; seq[1] = '['; seq[2] = 'M';
                seq[3] = 32 + (d > 0 ? 64 : 65);
                seq[4] = 32 + x; seq[5] = 32 + y;
                len = 6;
            }
            if (len > 0) write_to_pane(seq, len);
            return;
        }
        if (!s->in_alt_screen) do_scroll(d > 0 ? 3 : -3);
        return;
    }
    if (s->mouse_tracking == 0) {
        // v8.57: pane right-click no longer opens the context menu. v8.53
        // added this as a fallback for a stuck middle button, but the mbtn
        // priority fix (LEFT>RIGHT>MIDDLE) already makes right-click on the
        // TAB work reliably, so this fallback only caused "menu pops up when
        // right-clicking the content area" (user's report). The context menu
        // now opens ONLY on right-click of a tab.
        return;
    }
    int x = mx + 1, y = my;
    if (x < 1) x = 1;
    if (x > s->cols) x = s->cols;
    if (y < 1) y = 1;
    if (y > s->rows) y = s->rows;
    char seq[64]; int len = 0;

    static DWORD prev_btn_state = 0;
    DWORD changed = me->dwButtonState ^ prev_btn_state;
    DWORD released = changed & ~me->dwButtonState;
    prev_btn_state = me->dwButtonState;

    if (s->mouse_sgr) {
        int btn = 0; char act = 'M';
        if (me->dwEventFlags == 0 || me->dwEventFlags == DOUBLE_CLICK) {
            if (me->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) btn = 0;
            else if (me->dwButtonState & RIGHTMOST_BUTTON_PRESSED) btn = 2;
            else if (me->dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) btn = 1;
            else {
                act = 'm';
                if (released & RIGHTMOST_BUTTON_PRESSED) btn = 2;
                else if (released & FROM_LEFT_2ND_BUTTON_PRESSED) btn = 1;
                else btn = 0;
            }
        } else if (me->dwEventFlags == MOUSE_MOVED) {
            if (s->mouse_tracking < 1002) return;
            if (s->mouse_tracking == 1002 && !(me->dwButtonState & (FROM_LEFT_1ST_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED | FROM_LEFT_2ND_BUTTON_PRESSED)))
                return;
            btn = 32;
            if (me->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) btn += 0;
            else if (me->dwButtonState & RIGHTMOST_BUTTON_PRESSED) btn += 2;
            else if (me->dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) btn += 1;
            else btn += 3;
        } else return;
        if (me->dwControlKeyState & SHIFT_PRESSED) btn |= 4;
        if (me->dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) btn |= 8;
        if (me->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) btn |= 16;
        len = snprintf(seq, sizeof(seq), "\x1b[<%d;%d;%d%c", btn, x, y, act);
    }
    else {
        int btn = 0;
        if (me->dwEventFlags == 0 || me->dwEventFlags == DOUBLE_CLICK) {
            if (me->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) btn = 0;
            else if (me->dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) btn = 1;
            else if (me->dwButtonState & RIGHTMOST_BUTTON_PRESSED) btn = 2;
            else btn = 3;
        } else if (me->dwEventFlags == MOUSE_MOVED) {
            if (s->mouse_tracking < 1002) return;
            btn = 32;
            if (me->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) btn += 0;
            else if (me->dwButtonState & RIGHTMOST_BUTTON_PRESSED) btn += 2;
            else if (me->dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) btn += 1;
            else if (s->mouse_tracking < 1003) return;
            else btn += 3;
        } else return;
        if (me->dwControlKeyState & SHIFT_PRESSED) btn |= 4;
        if (me->dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) btn |= 8;
        if (me->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) btn |= 16;
        if (x > 223 || y > 223) return;
        seq[0] = '\x1b'; seq[1] = '['; seq[2] = 'M';
        seq[3] = 32 + btn; seq[4] = 32 + x; seq[5] = 32 + y; len = 6;
    }
    if (len > 0) write_to_pane(seq, len);
}

static void handle_resize(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(g_mux.hOut, &csbi)) return;   // v7: guard
    int nc = csbi.srWindow.Right - csbi.srWindow.Left + 1, nt = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    if (nc < 4) nc = 4;          // v7: degenerate sizes (minimized/zeroed) could
    if (nt < 2) nt = 2;          //     crash rendering or ConPTY resize
    int nr = nt - 1;
    if (nc == g_mux.host_cols && nt == g_mux.total_host_rows) return;
    g_mux.host_cols = nc; g_mux.total_host_rows = nt; g_mux.host_rows = nr;
    for (int i = 0; i < g_mux.pane_count; i++) if (g_mux.panes[i].active) {
        EnterCriticalSection(&g_mux.cs);
        screen_resize(&g_mux.panes[i].screen, nc, nr);
        g_mux.panes[i].screen.detect_count = 0;   // v8.3: host resized - allow re-adapt
        // v7: clamp scroll offset to the (possibly shorter) new scrollback;
        // v8.14: use hist_lines (real depth) - screen_resize resets it to 0,
        // so any stale offset is cleared instead of scrolling into empty space.
        if (g_mux.panes[i].scroll_offset > g_mux.panes[i].screen.hist_lines)
            g_mux.panes[i].scroll_offset = g_mux.panes[i].screen.hist_lines;
        LeaveCriticalSection(&g_mux.cs);
        COORD sz = {(SHORT)nc, (SHORT)nr};
        ResizePseudoConsole(g_mux.panes[i].hpc, sz);
    }
    g_mux.needs_redraw = 1;
}

static void handle_input(void) {
    INPUT_RECORD rec[128]; DWORD cnt;
    while (g_mux.running) {
        if (WaitForSingleObject(g_mux.hIn, 30) == WAIT_OBJECT_0 && ReadConsoleInputW(g_mux.hIn, rec, 128, &cnt))
            for (DWORD i = 0; i < cnt; i++) { if (rec[i].EventType == KEY_EVENT) handle_key(&rec[i].Event.KeyEvent); else if (rec[i].EventType == MOUSE_EVENT) handle_mouse(&rec[i].Event.MouseEvent); else if (rec[i].EventType == WINDOW_BUFFER_SIZE_EVENT) handle_resize(); }
        // v7: reap panes whose child processes died, then make sure the active
        // pane is still valid (otherwise switch to the first live one).
        reap_dead_panes();
        if (g_mux.active_pane < 0 || !g_mux.panes[g_mux.active_pane].active) {
            int f = -1;
            for (int i = 0; i < g_mux.pane_count; i++) if (g_mux.panes[i].active) { f = i; break; }
            if (f >= 0) g_mux.active_pane = f; else { g_mux.running = 0; break; }
        }
        if (g_mux.needs_redraw) render_screen();
    }
}

// ============================================================
// Console control handler (v7): restore terminal on Ctrl+C / close
// ============================================================
static BOOL WINAPI ctrl_handler(DWORD type) {
    (void)type;
    // Tell the main loop to stop; cleanup in main() restores all console state.
    InterlockedExchange(&g_mux.running, 0);
    return TRUE;   // don't let the default handler kill us before cleanup
}

// ============================================================
// Main
// ============================================================
int main(void) {
    memset(&g_mux, 0, sizeof(g_mux)); InitializeCriticalSection(&g_mux.cs);
    g_mux.hOut = GetStdHandle(STD_OUTPUT_HANDLE); g_mux.hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (g_mux.hOut == INVALID_HANDLE_VALUE || g_mux.hIn == INVALID_HANDLE_VALUE ||
        g_mux.hOut == NULL || g_mux.hIn == NULL) {
        fprintf(stderr, "termux: no console attached (run from a console window)\n");
        DeleteCriticalSection(&g_mux.cs);
        return 1;
    }
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(g_mux.hOut, &csbi)) {
        fprintf(stderr, "termux: cannot query console buffer\n");
        DeleteCriticalSection(&g_mux.cs);
        return 1;
    }
    g_mux.host_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1; g_mux.total_host_rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1; g_mux.host_rows = g_mux.total_host_rows - 1;
    if (g_mux.host_cols < 4) g_mux.host_cols = 4;
    if (g_mux.total_host_rows < 2) g_mux.total_host_rows = 2;
    g_mux.host_rows = g_mux.total_host_rows - 1;

    GetConsoleMode(g_mux.hIn, &g_mux.orig_in_mode); GetConsoleMode(g_mux.hOut, &g_mux.orig_out_mode);
    GetConsoleTitleW(g_orig_title, 255);
    DWORD im = g_mux.orig_in_mode; im &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE); im |= ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS; SetConsoleMode(g_mux.hIn, im);
    DWORD om = g_mux.orig_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT | DISABLE_NEWLINE_AUTO_RETURN; SetConsoleMode(g_mux.hOut, om);
    g_mux.orig_cp = GetConsoleOutputCP(); g_mux.orig_input_cp = GetConsoleCP(); SetConsoleOutputCP(65001); SetConsoleCP(65001);
    g_dump_enabled = getenv("TERMUX_DUMP") != NULL;   // v8.2: optional raw dump
    if (g_dump_enabled) {
        // v8.54: startup marker in mouse_dump.log - proves WHICH binary ran and
        // when (the user twice reported missing mouse_dump.log while an old exe
        // was still in use; a fresh "[v8.54] startup" line ends that confusion).
        FILE *f = fopen("mouse_dump.log", "ab");
        if (f) { fprintf(f, "[v8.54] startup host=%dx%d\n", g_mux.host_cols, g_mux.host_rows); fclose(f); }
    }
    SetConsoleCtrlHandler(ctrl_handler, TRUE);   // v7: restore console on Ctrl+C/close
    load_config();                               // load custom menu items from termux.ini

    host_printf("\x1b[?1049h\x1b[2J\x1b[H");
    host_printf("\x1b[36;1m Windows Terminal Multiplexer v1.1.1\x1b[0m\r\n");
    host_printf("  \x1b[33mhost: %dx%d\x1b[0m   (pane screen = host minus 1 tab bar row)\r\n\n", g_mux.host_cols, g_mux.host_rows);
    host_printf("  \x1b[33mCtrl+B\x1b[0m + c/n/p/x/d/0-9   (termux = 帮助)\r\n\n");
    host_printf("  \x1b[33m右键\x1b[0m 标签 = 改颜色、改标题\r\n\n");
    host_printf("  \x1b[38;2;248;81;73m[注意]\x1b[0m 终端请使用等宽字体，否则会发生渲染故障\r\n\n");
    // v8.56: removed the fixed Sleep(800) splash delay - "Starting..." now
    // proceeds straight into pane creation (ConPTY+cmd take ~100-300ms on
    // their own; the extra 800ms was pure dead time).
    host_printf("Starting...\r\n");
    g_mux.running = 1;
    int first = create_pane();
    if (first < 0) { host_printf("\x1b[31mFailed! Need Win10 1809+ and enough memory\x1b[0m\r\n"); Sleep(3000); goto cleanup; }
    g_mux.active_pane = first; g_mux.needs_redraw = 1;
    handle_input();
    for (int i = 0; i < g_mux.pane_count; i++) close_pane(i);   // v7: close ALL panes (live or dead)
cleanup:
    host_printf("\x1b[?1049l\x1b[0m");
    if (g_orig_title[0]) {
        SetConsoleTitleW(g_orig_title);
        char tbuf[512];
        int tl = WideCharToMultiByte(CP_UTF8, 0, g_orig_title, -1, tbuf, sizeof(tbuf), NULL, NULL);
        if (tl > 0) host_printf("\x1b]0;%s\x07", tbuf);
    }
    SetConsoleCtrlHandler(ctrl_handler, FALSE);
    SetConsoleMode(g_mux.hIn, g_mux.orig_in_mode); SetConsoleMode(g_mux.hOut, g_mux.orig_out_mode);
    SetConsoleOutputCP(g_mux.orig_cp); SetConsoleCP(g_mux.orig_input_cp);
    free(g_render_buf); g_render_buf = NULL; g_render_buf_cap = 0;
    DeleteCriticalSection(&g_mux.cs); printf("Bye!\n"); return 0;
}
