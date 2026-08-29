#ifndef WIN_TERMUX_TYPES_H
#define WIN_TERMUX_TYPES_H

#include "common.h"

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
    CHAR_INFO *cells;
    WORD *fg_rgb;
    WORD *bg_rgb;
    unsigned char *rgb_valid;
} ScreenLine;

typedef struct {
    ScreenLine *lines;
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
    int osc_sep;

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

    unsigned utf8_state, utf8_cp;
    int pane_index;
    int detect_col, detect_count;

    int fg_r, fg_g, fg_b, bg_r, bg_g, bg_b;
    int fg_rgb_on, bg_rgb_on;
    WORD *alt_fg_rgb, *alt_bg_rgb;
    unsigned char *alt_rgb_valid;
    int hist_lines;
    int alt_hist_lines;
} ScreenBuffer;

typedef struct {
    int start_col, end_col, pane_idx;
    int close_start, close_end;
} PaneTabInfo;

typedef struct {
    int active;
    HPCON hpc;
    HANDLE pipe_in, pipe_out, process, thread, read_thread;
    ScreenBuffer screen;
    char title[64];
    char full_title[256];
    int scroll_offset;
    int color;
    int is_settings;
    int is_about;
    int exited_hold;
    DWORD exit_code;
    WCHAR input_history[256];
    int input_history_len;
    int input_history_pos;
} Pane;

typedef struct {
    char name[32];
    char cmd[256];
    char workdir[256];
    int color;          /* 启动默认标签颜色：0 = 跟随默认(蓝)，1-8 = 指定色 */
} ChooserItem;

typedef struct {
    int abs_y;
    int start_x;
    int end_x;
} SearchMatch;

typedef struct {
    int page;
    int selection;
    int scroll;
    int query_len;
    int query_pos;
    int focus;
    char query[64];
} PaletteViewState;

typedef struct {
    Pane panes[MAX_PANES];
    int pane_count, active_pane;
    volatile LONG running;
    int host_cols, host_rows, total_host_rows;
    HANDLE hOut, hIn;
    CRITICAL_SECTION cs;
    int needs_redraw, prefix_mode;
    int help_mode;
    int help_scroll;
    int chooser_mode;
    int custom_cmd_mode;
    char custom_cmd_buf[128];
    int custom_cmd_len;
    int custom_cmd_pos;
    int ctx_mode;
    int ctx_pane;
    int rename_mode;
    char rename_buf[64];
    int rename_len;
    int rename_pos;
    int settings_mode;
    int settings_sel;
    int settings_edit_idx;
    int settings_edit_field;
    char settings_edit_name[32];
    int settings_edit_name_len;
    int settings_edit_name_pos;
    char settings_edit_cmd[256];
    int settings_edit_cmd_len;
    int settings_edit_cmd_pos;
    int palette_mode;
    int palette_page;
    PaletteViewState palette_stack[PALETTE_STACK_MAX];
    int palette_stack_len;
    int palette_sel;
    char palette_query[64];
    int palette_query_len;
    int palette_query_pos;
    int palette_scroll;
    int palette_focus;
    int palette_field;
    int confirm_exit_mode;   /* confirm_on_exit = true 时的退出确认弹窗 */
    int palette_edit_idx;
    int palette_edit_new;
    DWORD orig_in_mode, orig_out_mode;
    UINT orig_cp, orig_input_cp;
    PaneTabInfo tab_info[MAX_PANES + 3];
    int tab_count;
} MuxState;

// Global State Externs
extern MuxState g_mux;
extern int g_pop_anchor_x;
extern int g_mouse_x, g_mouse_y;
extern int g_mouse_prev_in_tabbar;
extern WCHAR g_high_surrogate;
extern WCHAR g_orig_title[256];

extern int g_hover_preview_pane;
extern DWORD64 g_hover_preview_start;
extern int g_hover_preview_active;
extern int g_hover_chooser_idx;
extern DWORD64 g_hover_chooser_start;
extern int g_hover_chooser_active;
extern int g_hover_settings_name_idx;
extern DWORD64 g_hover_settings_name_start;
extern int g_hover_settings_name_active;
extern int g_hover_settings_cmd_idx;
extern DWORD64 g_hover_settings_cmd_start;
extern int g_hover_settings_cmd_active;

extern int g_sb_dragging;
extern int g_sb_grab_offset;

// Copy Mode & Selection
extern int g_copy_mode;
extern int g_copy_sel_active;
extern int g_copy_cx, g_copy_cy;
extern int g_copy_anchor_x, g_copy_anchor_abs_y;
extern int g_copy_block;   /* 1 = 矩形（框）选区，0 = 行内连续选区 */
extern int g_copy_quick;   /* Shift/Alt 点选发起的临时复制会话 */
/* 复制 / 搜索这两个模态属于某一个 pane：切到别的标签页必须先收回，
 * 否则两个标签页会同时响应同一套按键。-1 = 当前没有模态。 */
extern int g_ui_mode_pane;
extern int g_mouse_selecting;
extern int g_mouse_sel_sx, g_mouse_sel_s_abs_y;
extern int g_mouse_sel_ex, g_mouse_sel_e_abs_y;

// Scrollback History Search
extern SearchMatch g_search_matches[MAX_SEARCH_MATCHES];
extern int g_search_match_count;
extern int g_search_match_cur;
extern int g_search_mode;
extern int g_search_active;
extern char g_search_buf[64];
extern int g_search_len, g_search_pos;

// Diagnostic functions
void dump_pane_bytes(int pane_idx, const char *data, int len);
void dump_render_output(const char *out, int len, int pcols, int prows, int hcols, int hrows);
void dump_delta_output(const char *data, int delta_len, int full_len);
void log_mouse_event(const char *tag, const MOUSE_EVENT_RECORD *me);
void host_write(const char *data, int len);

#endif // WIN_TERMUX_TYPES_H
