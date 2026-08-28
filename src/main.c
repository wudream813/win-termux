#include "common.h"
#include "types.h"
#include "screen.h"
#include "utf8.h"
#include "vt.h"
#include "config.h"
#include "pane.h"
#include "render.h"
#include "input.h"

// Global variable definitions
MuxState g_mux;
int g_pop_anchor_x = -1;
int g_mouse_x = -1, g_mouse_y = -1;
int g_mouse_prev_in_tabbar = 0;
WCHAR g_high_surrogate = 0;
WCHAR g_orig_title[256] = {0};

int g_hover_preview_pane = -1;
DWORD64 g_hover_preview_start = 0;
int g_hover_preview_active = 0;
int g_hover_chooser_idx = -1;
DWORD64 g_hover_chooser_start = 0;
int g_hover_chooser_active = 0;
int g_hover_settings_name_idx = -1;
DWORD64 g_hover_settings_name_start = 0;
int g_hover_settings_name_active = 0;
int g_hover_settings_cmd_idx = -1;
DWORD64 g_hover_settings_cmd_start = 0;
int g_hover_settings_cmd_active = 0;

int g_sb_dragging = 0;
int g_sb_grab_offset = 0;

// Copy Mode & Selection
int g_copy_mode = 0;
int g_copy_sel_active = 0;
int g_copy_cx = 0, g_copy_cy = 0;
int g_copy_anchor_x = 0, g_copy_anchor_abs_y = 0;
int g_copy_block = 0;
int g_copy_quick = 0;
int g_mouse_selecting = 0;
int g_mouse_sel_sx = 0, g_mouse_sel_s_abs_y = 0;
int g_mouse_sel_ex = 0, g_mouse_sel_e_abs_y = 0;

// Scrollback History Search
SearchMatch g_search_matches[MAX_SEARCH_MATCHES];
int g_search_match_count = 0;
int g_search_match_cur = -1;
int g_search_mode = 0;
int g_search_active = 0;
char g_search_buf[64] = {0};
int g_search_len = 0, g_search_pos = 0;

static int g_dump_enabled = 0;
static int g_mouse_log_moves = 0;

void host_write(const char *s, int len) {
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
    if (len > 0) {
        if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
        host_write(buf, len);
    }
}

void dump_pane_bytes(int idx, const char *data, int len) {
    if (!g_dump_enabled || len <= 0) return;
    FILE *f = fopen("termux_dump.log", "ab");
    if (!f) return;
    fprintf(f, "[pane %d len %d]\n", idx, len);
    fwrite(data, 1, (size_t)len, f);
    fputc('\n', f);
    fclose(f);
}

void dump_render_output(const char *data, int len, int mcols, int mrows, int hcols, int hrows) {
    if (!g_dump_enabled || len <= 0) return;
    FILE *f = fopen("render_dump.log", "ab");
    if (!f) return;
    fprintf(f, "[render len %d model %dx%d host %dx%d]\n", len, mcols, mrows, hcols, hrows);
    fwrite(data, 1, (size_t)len, f);
    fputc('\n', f);
    fclose(f);
}

void log_mouse_event(const char *tag, const MOUSE_EVENT_RECORD *me) {
    if (!g_dump_enabled) return;
    unsigned btn = (unsigned)me->dwButtonState;
    int is_press = (btn & (FROM_LEFT_1ST_BUTTON_PRESSED | FROM_LEFT_2ND_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED)) &&
                   (me->dwEventFlags == 0 || me->dwEventFlags == DOUBLE_CLICK);
    int is_release = (btn & 0x7) == 0 && me->dwEventFlags == 0;
    if (me->dwEventFlags == MOUSE_MOVED) {
        if (++g_mouse_log_moves < 20) return;
        g_mouse_log_moves = 0;
    }
    if (me->dwEventFlags == MOUSE_WHEELED || me->dwEventFlags == MOUSE_HWHEELED) return;
    FILE *f = fopen("mouse_dump.log", "ab");
    if (!f) return;
    fprintf(f, "[v8.54] %s pos=%d,%d flags=%u btn=0x%X ctrl=0x%X%s | chooser=%d ctx=%d rename=%d help=%d pop_anchor=%d mouse=%d,%d tab_count=%d\n",
            tag, (int)me->dwMousePosition.X, (int)me->dwMousePosition.Y,
            (unsigned)me->dwEventFlags, btn, (unsigned)me->dwControlKeyState,
            is_press ? " PRESS" : (is_release ? " RELEASE" : ""),
            g_mux.chooser_mode, g_mux.ctx_mode, g_mux.rename_mode, g_mux.help_mode,
            g_pop_anchor_x, g_mouse_x, g_mouse_y, g_mux.tab_count);
    if (is_press) {
        for (int i = 0; i < g_mux.tab_count; i++) {
            PaneTabInfo *t = &g_mux.tab_info[i];
            fprintf(f, "  tab[%d] pane=%d cols[%d,%d) close[%d,%d)\n", i, t->pane_idx,
                    t->start_col, t->end_col, t->close_start, t->close_end);
        }
    }
    fclose(f);
}

static void handle_resize(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(g_mux.hOut, &csbi)) return;
    int nc = csbi.srWindow.Right - csbi.srWindow.Left + 1, nt = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    if (nc < 4) nc = 4;
    if (nt < 2) nt = 2;
    int nr = nt - 1;
    if (nc == g_mux.host_cols && nt == g_mux.total_host_rows) return;

    EnterCriticalSection(&g_mux.cs);
    g_mux.host_cols = nc; g_mux.total_host_rows = nt; g_mux.host_rows = nr;
    int pane_cols = nc;
    for (int i = 0; i < g_mux.pane_count; i++) if (g_mux.panes[i].active) {
        screen_resize(&g_mux.panes[i].screen, pane_cols, nr);
        g_mux.panes[i].screen.detect_count = 0;
        if (g_mux.panes[i].scroll_offset > g_mux.panes[i].screen.hist_lines)
            g_mux.panes[i].scroll_offset = g_mux.panes[i].screen.hist_lines;
    }
    LeaveCriticalSection(&g_mux.cs);

    for (int i = 0; i < g_mux.pane_count; i++) if (g_mux.panes[i].active) {
        if (g_mux.panes[i].hpc) {
            COORD sz = {(SHORT)pane_cols, (SHORT)nr};
            ResizePseudoConsole(g_mux.panes[i].hpc, sz);
        }
    }
    g_mux.needs_redraw = 1;
}

static void handle_input(void) {
    INPUT_RECORD rec[128]; DWORD cnt;
    ULONGLONG last_render = 0;
    while (g_mux.running) {
        DWORD wait_ms = g_mux.needs_redraw ? 8 : 25;
        int has_input = (WaitForSingleObject(g_mux.hIn, wait_ms) == WAIT_OBJECT_0 && ReadConsoleInputW(g_mux.hIn, rec, 128, &cnt));
        if (has_input) {
            for (DWORD i = 0; i < cnt; i++) {
                if (rec[i].EventType == KEY_EVENT) handle_key(&rec[i].Event.KeyEvent);
                else if (rec[i].EventType == MOUSE_EVENT) handle_mouse(&rec[i].Event.MouseEvent);
                else if (rec[i].EventType == WINDOW_BUFFER_SIZE_EVENT) handle_resize();
            }
        }

        // v1.1.5: hover preview 1.5s timer check
        if (g_hover_preview_pane >= 0 && !g_hover_preview_active) {
            if (GetTickCount64() - g_hover_preview_start >= 1500) {
                g_hover_preview_active = 1;
                g_mux.needs_redraw = 1;
            }
        }

        // v1.2.8: chooser item hover preview 1.0s timer check
        if (g_hover_chooser_idx >= 0 && !g_hover_chooser_active) {
            if (GetTickCount64() - g_hover_chooser_start >= 1000) {
                g_hover_chooser_active = 1;
                g_mux.needs_redraw = 1;
            }
        }

        // v1.2.7: settings name hover preview 1.0s timer check
        if (g_hover_settings_name_idx >= 0 && !g_hover_settings_name_active) {
            if (GetTickCount64() - g_hover_settings_name_start >= 1000) {
                g_hover_settings_name_active = 1;
                g_mux.needs_redraw = 1;
            }
        }

        // v1.2.4: settings cmd hover preview 1.0s timer check
        if (g_hover_settings_cmd_idx >= 0 && !g_hover_settings_cmd_active) {
            if (GetTickCount64() - g_hover_settings_cmd_start >= 1000) {
                g_hover_settings_cmd_active = 1;
                g_mux.needs_redraw = 1;
            }
        }

        reap_dead_panes();
        if (g_mux.active_pane < 0 || !g_mux.panes[g_mux.active_pane].active) {
            int f = -1;
            for (int i = 0; i < g_mux.pane_count; i++) if (g_mux.panes[i].active) { f = i; break; }
            if (f >= 0) g_mux.active_pane = f; else { g_mux.running = 0; break; }
        }
        if (g_mux.needs_redraw) {
            ULONGLONG now = GetTickCount64();
            if (has_input || (now - last_render >= 12)) {
                render_screen();
                last_render = now;
            }
        }
    }
}

static BOOL WINAPI ctrl_handler(DWORD type) {
    (void)type;
    InterlockedExchange(&g_mux.running, 0);
    return TRUE;
}

int main(void) {
    memset(&g_mux, 0, sizeof(g_mux));
    InitializeCriticalSection(&g_mux.cs);

    g_mux.hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    g_mux.hIn = GetStdHandle(STD_INPUT_HANDLE);
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
    g_mux.host_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    g_mux.total_host_rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    g_mux.host_rows = g_mux.total_host_rows - 1;
    if (g_mux.host_cols < 4) g_mux.host_cols = 4;
    if (g_mux.total_host_rows < 2) g_mux.total_host_rows = 2;
    g_mux.host_rows = g_mux.total_host_rows - 1;

    GetConsoleMode(g_mux.hIn, &g_mux.orig_in_mode);
    GetConsoleMode(g_mux.hOut, &g_mux.orig_out_mode);
    GetConsoleTitleW(g_orig_title, 255);
    DWORD im = g_mux.orig_in_mode;
    im &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE);
    im |= ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS;
    SetConsoleMode(g_mux.hIn, im);
    DWORD om = g_mux.orig_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT | DISABLE_NEWLINE_AUTO_RETURN;
    SetConsoleMode(g_mux.hOut, om);
    g_mux.orig_cp = GetConsoleOutputCP();
    g_mux.orig_input_cp = GetConsoleCP();
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    g_dump_enabled = getenv("TERMUX_DUMP") != NULL;
    if (g_dump_enabled) {
        FILE *f = fopen("mouse_dump.log", "ab");
        if (f) {
            fprintf(f, "[v8.54] startup host=%dx%d\n", g_mux.host_cols, g_mux.host_rows);
            fclose(f);
        }
    }
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
    load_config();

    /* mouse = false 时既不申请控制台鼠标事件，也不打开 VT 鼠标追踪 */
    if (!g_mouse_enabled) SetConsoleMode(g_mux.hIn, im & ~(DWORD)ENABLE_MOUSE_INPUT);
    if (g_mouse_enabled)
        host_printf("\x1b[?1049h\x1b[?1003h\x1b[?1006h\x1b[2J\x1b[H\x1b[?25l");
    else
        host_printf("\x1b[?1049h\x1b[2J\x1b[H\x1b[?25l");
    g_mux.running = 1;
    int first = create_pane();
    if (first < 0) {
        host_printf("\x1b[31mFailed! Need Win10 1809+ and enough memory\x1b[0m\r\n");
        Sleep(3000);
        goto cleanup;
    }
    g_mux.active_pane = first;
    if (g_default_startup == 1) {
        g_mux.help_mode = 1;
    }
    g_mux.needs_redraw = 1;
    render_screen();
    handle_input();
    for (int i = 0; i < g_mux.pane_count; i++) close_pane(i);

cleanup:
    host_printf("\x1b[?1003l\x1b[?1006l\x1b[?1049l\x1b[?25h\x1b[0m");
    if (g_orig_title[0]) {
        SetConsoleTitleW(g_orig_title);
        char tbuf[512];
        int tl = WideCharToMultiByte(CP_UTF8, 0, g_orig_title, -1, tbuf, sizeof(tbuf), NULL, NULL);
        if (tl > 0) host_printf("\x1b]0;%s\x07", tbuf);
    }
    SetConsoleCtrlHandler(ctrl_handler, FALSE);
    SetConsoleMode(g_mux.hIn, g_mux.orig_in_mode);
    SetConsoleMode(g_mux.hOut, g_mux.orig_out_mode);
    SetConsoleOutputCP(g_mux.orig_cp);
    SetConsoleCP(g_mux.orig_input_cp);
    render_cleanup();
    DeleteCriticalSection(&g_mux.cs);
    printf("Bye!\n");
    return 0;
}
