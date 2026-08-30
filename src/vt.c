#include "vt.h"

static void screen_put_cp(ScreenBuffer *s, unsigned int cp) {
    if (s->wraparound_pending) {
        s->cursor_x = 0;
        screen_newline(s);
        s->wraparound_pending = 0;
    }
    int wide = is_wide_cp(cp);
    if (wide && s->cursor_x >= s->cols - 1) {
        /* 宽字符在只剩一列（cursor_x == cols-1）时放不下，要整体移到下一行
         * 行首。此时旧行最后一列（cursor_x == cols-1）放不下宽字的两格，
         * 必须显式清成空格：否则它会保留上一帧的脏内容（可能是空格、旧字符，
         * 甚至是 ConPTY 重绘时残留的宽字次格 0 / 主格）。吸附函数
         * snap_left_to_char 只看「本格 ch==0 且左邻是宽字主格」就把该格认成
         * 宽字次格而左退一列——一旦这条「因汉字换行」的脏行出现在块选里，
         * 选区左沿在那一行被错误吸附；更常见的是脏末格被渲染/复制路径按宽字
         * 处理，造成经过该行之后所有行的高亮整体错位一列。清成空格后它就是
         * 普通空白，吸附与渲染都不会再误判。 */
        if (s->cursor_x == s->cols - 1) {
            WORD attr = build_attr(s);
            screen_write_cell(s, s->cursor_y, s->cursor_x, L' ', attr);
        }
        s->cursor_x = 0;
        screen_newline(s);
        s->wraparound_pending = 0;
    }
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
        if (s->detect_count <= 100) detect_conpty_width(s, 0);
    }
}

static void screen_send_response(ScreenBuffer *s, const char *resp) {
    int len = (int)strlen(resp);
    if (len < (int)sizeof(s->response_buf)) {
        memcpy(s->response_buf, resp, len);
        s->response_len = len;
    }
}

static int parse_params(const char *buf, int len, int *params, int max) {
    int count = 0, val = 0, has = 0;
    for (int i = 0; i < len && count < max; i++) {
        char c = buf[i];
        if (c >= '0' && c <= '9') {
            val = val * 10 + (c - '0');
            if (val > 9999999) val = 9999999;
            has = 1;
        }
        else if (c == ';' || c == ':') { params[count++] = has ? val : 0; val = 0; has = 0; }
    }
    if ((has || count > 0) && count < max) params[count++] = has ? val : 0;
    return count;
}

void process_sgr(ScreenBuffer *s, const int *p, int n) {
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
                    else if (c < 232) { c -= 16; s->bg_color = ((c/36)>2?1:0)|((c/6%6)>2?2:0)|((c%6)>2?4:0); if((c/36)>3||(c/6%6)>3||(c%6)>3) s->bg_color|=8; }
                    else s->bg_color = (c-232)>12?15:0;
                    s->bg_rgb_on = 0;
                    i += 2;
                } else if (v == 38 && i + 4 < n && p[i+1] == 2) {
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

void sanitize_title(const char *raw, int raw_len, char *out, int out_size) {
    if (!raw || raw_len <= 0 || out_size <= 0) {
        if (out && out_size > 0) out[0] = 0;
        return;
    }
    char buf[512];
    int len = raw_len < 511 ? raw_len : 511;
    memcpy(buf, raw, len);
    buf[len] = 0;

    while (len > 0 && ((unsigned char)buf[len - 1] <= ' ' || buf[len - 1] == 0x07)) {
        buf[--len] = 0;
    }

    const char *p = buf;

    if (strncmp(p, "\xe7\xae\xa1\xe7\x90\x86\xe5\x91\x98", 9) == 0) {
        p += 9;
        while (*p == ':' || *p == ' ') p++;
    } else if (ci_str_starts_with(p, "Administrator")) {
        p += 13;
        while (*p == ':' || *p == ' ') p++;
    }

    const char *colon = strstr(p, ":   ");
    if (!colon) colon = strstr(p, ":  ");
    if (!colon) colon = strstr(p, ": ");
    if (colon) {
        p = colon + 1;
        while (*p == ' ' || *p == ':') p++;
    }

    const char *dash = strstr(p, " - ");
    if (dash && (strstr(buf, ".exe") || strstr(buf, "\\") || strstr(buf, "/"))) {
        p = dash + 3;
        while (*p == ' ') p++;
    }

    while (*p == ':' || *p == '-' || *p == ' ') p++;

    if (strstr(p, "\\") || strstr(p, "/")) {
        const char *last_slash = p;
        for (const char *sp = p; *sp; sp++) {
            if (*sp == '\\' || *sp == '/') last_slash = sp + 1;
        }
        p = last_slash;
    }

    if (ci_str_eq(p, "cmd.exe") || ci_str_eq(p, "cmd")) {
        p = "cmd";
    } else if (ci_str_eq(p, "powershell.exe") || ci_str_eq(p, "powershell")) {
        p = "PowerShell";
    }

    if (!*p) p = "cmd";

    snprintf(out, out_size, "%s", p);
}

void execute_osc(ScreenBuffer *s) {
    if ((s->osc_num == 0 || s->osc_num == 1 || s->osc_num == 2) && s->osc_len > 0) {
        int idx = s->pane_index;
        if (idx >= 0 && idx < g_mux.pane_count && g_mux.panes[idx].active) {
            if (!g_mux.panes[idx].is_about && !g_mux.panes[idx].is_settings) {
                char raw[256];
                int rlen = s->osc_len < 255 ? s->osc_len : 255;
                memcpy(raw, s->osc_buf, rlen);
                raw[rlen] = 0;
                while (rlen > 0 && ((unsigned char)raw[rlen - 1] <= ' ' || raw[rlen - 1] == 0x07)) raw[--rlen] = 0;
                snprintf(g_mux.panes[idx].full_title, sizeof(g_mux.panes[idx].full_title), "%s", raw);

                sanitize_title(s->osc_buf, s->osc_len, g_mux.panes[idx].title, sizeof(g_mux.panes[idx].title));
            }
        }
    }
    s->osc_num = -1; s->osc_len = 0; s->osc_sep = 0;
}

void execute_esc(ScreenBuffer *s, char final, const char *inter, int inter_len) {
    (void)inter;
    if (inter_len > 0) return;

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

static void execute_csi_internal(ScreenBuffer *s, char final, char prefix, const char *params_str, int params_len, const char *inter, int inter_len) {
    (void)inter;
    int params[32] = {0};
    int pc = parse_params(params_str, params_len, params, 32);
    int p1 = pc > 0 ? params[0] : 0;
    int p2 = pc > 1 ? params[1] : 0;

    if (inter_len > 0) return;

    if (prefix == '?') {
        if (final == 'h') {
            for (int i = 0; i < pc; i++) {
                switch (params[i]) {
                    case 1: s->app_cursor_keys = 1; break;
                    case 7: s->auto_wrap = 1; break;
                    case 25: s->cursor_visible = 1; break;
                    case 47: case 1047:
                        if (!s->in_alt_screen) {
                            s->in_alt_screen = 1; s->alt_scroll_top = s->scroll_top;
                            s->alt_hist_lines = s->hist_lines;
                        }
                        for (int j = 0; j < s->rows * s->cols; j++) {
                            s->alt_buffer[j].Char.UnicodeChar = L' '; s->alt_buffer[j].Attributes = s->current_attr;
                            if (s->alt_fg_rgb) { s->alt_fg_rgb[j] = RGB565_WHITE; s->alt_bg_rgb[j] = RGB565_BLACK; s->alt_rgb_valid[j] = 0; }
                        }
                        { int pi = s->pane_index; if (pi >= 0 && pi < MAX_PANES) g_mux.panes[pi].scroll_offset = 0; }
                        break;
                    case 1049:
                        s->saved_cx = s->cursor_x; s->saved_cy = s->cursor_y;
                        if (!s->in_alt_screen) {
                            s->in_alt_screen = 1; s->alt_scroll_top = s->scroll_top;
                            s->alt_hist_lines = s->hist_lines;
                        }
                        for (int j = 0; j < s->rows * s->cols; j++) {
                            s->alt_buffer[j].Char.UnicodeChar = L' '; s->alt_buffer[j].Attributes = s->current_attr;
                            if (s->alt_fg_rgb) { s->alt_fg_rgb[j] = RGB565_WHITE; s->alt_bg_rgb[j] = RGB565_BLACK; s->alt_rgb_valid[j] = 0; }
                        }
                        s->cursor_x = s->cursor_y = 0;
                        { int pi = s->pane_index; if (pi >= 0 && pi < MAX_PANES) g_mux.panes[pi].scroll_offset = 0; }
                        break;
                    case 1048: s->saved_cx = s->cursor_x; s->saved_cy = s->cursor_y; break;
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
                            s->hist_lines = s->alt_hist_lines;
                            int pi2 = s->pane_index;
                            if (pi2 >= 0 && pi2 < MAX_PANES && g_mux.panes[pi2].active) {
                                if (g_mux.panes[pi2].scroll_offset > s->hist_lines) g_mux.panes[pi2].scroll_offset = s->hist_lines;
                            }
                        }
                        break;
                    case 1049:
                        if (s->in_alt_screen) {
                            s->in_alt_screen = 0; s->scroll_top = s->alt_scroll_top;
                            s->hist_lines = s->alt_hist_lines;
                            s->cursor_x = s->saved_cx; s->cursor_y = s->saved_cy;
                            int pi2 = s->pane_index;
                            if (pi2 >= 0 && pi2 < MAX_PANES && g_mux.panes[pi2].active) {
                                if (g_mux.panes[pi2].scroll_offset > s->hist_lines) g_mux.panes[pi2].scroll_offset = s->hist_lines;
                            }
                        }
                        break;
                    case 1048: s->cursor_x = s->saved_cx; s->cursor_y = s->saved_cy; s->wraparound_pending = 0; break;
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
            if (p1 <= 1 && p2 <= 1) s->detect_count = 0;
            break;
        }
        case 'J': {
            WORD attr = build_attr(s);
            if (p1 == 0 || p1 == 2) {
                int sy = (p1 == 0) ? s->cursor_y : 0, sx = (p1 == 0) ? s->cursor_x : 0;
                for (int y = sy; y < s->rows; y++)
                    for (int x = (y == sy ? sx : 0); x < s->cols; x++)
                        screen_write_cell(s, y, x, L' ', attr);
            }
            if (p1 == 1) {
                for (int y = 0; y <= s->cursor_y; y++) {
                    int ex = (y == s->cursor_y) ? s->cursor_x : s->cols - 1;
                    for (int x = 0; x <= ex; x++) screen_write_cell(s, y, x, L' ', attr);
                }
            }
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
                int nr = p2, nc = params[2];
                if (nr >= 2 && nr <= 500 && nc >= 2 && nc <= 1000)
                    screen_resize(s, nc, nr);
            }
            break;
        case 'g': if (p1 == 0 && s->cursor_x < 512) s->tab_stops[s->cursor_x] = 0; else if (p1 == 3) memset(s->tab_stops, 0, sizeof(s->tab_stops)); break;
        case 'h':
            for (int i = 0; i < pc; i++) {
                if (params[i] == 47 || params[i] == 1047 || params[i] == 1049) {
                    if (!s->in_alt_screen) {
                        s->saved_cx = s->cursor_x; s->saved_cy = s->cursor_y;
                        s->in_alt_screen = 1; s->alt_scroll_top = s->scroll_top;
                        s->alt_hist_lines = s->hist_lines;
                    }
                    for (int j = 0; j < s->rows * s->cols; j++) {
                        s->alt_buffer[j].Char.UnicodeChar = L' '; s->alt_buffer[j].Attributes = s->current_attr;
                        if (s->alt_fg_rgb) { s->alt_fg_rgb[j] = RGB565_WHITE; s->alt_bg_rgb[j] = RGB565_BLACK; s->alt_rgb_valid[j] = 0; }
                    }
                    if (params[i] == 1049) s->cursor_x = s->cursor_y = 0;
                    int pi = s->pane_index;
                    if (pi >= 0 && pi < MAX_PANES && g_mux.panes[pi].active) {
                        g_mux.panes[pi].scroll_offset = 0;
                    }
                }
            }
            break;
        case 'l':
            for (int i = 0; i < pc; i++) {
                if (params[i] == 47 || params[i] == 1047 || params[i] == 1049) {
                    if (s->in_alt_screen) {
                        s->in_alt_screen = 0; s->scroll_top = s->alt_scroll_top;
                        s->hist_lines = s->alt_hist_lines;
                        if (params[i] == 1049) { s->cursor_x = s->saved_cx; s->cursor_y = s->saved_cy; }
                        int pi2 = s->pane_index;
                        if (pi2 >= 0 && pi2 < MAX_PANES && g_mux.panes[pi2].active) {
                            if (g_mux.panes[pi2].scroll_offset > s->hist_lines) g_mux.panes[pi2].scroll_offset = s->hist_lines;
                        }
                    }
                }
            }
            break;
    }
}

static inline int is_param_byte(unsigned char c) { return c >= 0x30 && c <= 0x3F; }
static inline int is_inter_byte(unsigned char c) { return c >= 0x20 && c <= 0x2F; }
static inline int is_final_byte(unsigned char c) { return c >= 0x40 && c <= 0x7E; }
static inline int is_c0(unsigned char c) { return c < 0x20 || c == 0x7F; }

static void screen_process_byte(ScreenBuffer *s, unsigned char c) {
    if (c == 0x18 || c == 0x1A) { s->state = ST_NORMAL; return; }
    if (c == 0x1B) {
        s->state = ST_ESC;
        s->param_len = 0;
        s->inter_len = 0;
        return;
    }

    switch (s->state) {
        case ST_NORMAL:
            if (c < 0x20) {
                switch (c) {
                    case 0x07: break;
                    case 0x08: if (s->cursor_x > 0) s->cursor_x--; s->wraparound_pending = 0; break;
                    case 0x09: { int x = s->cursor_x + 1; while (x < s->cols && x < 512 && !s->tab_stops[x]) x++; s->cursor_x = (x < s->cols) ? x : s->cols - 1; s->wraparound_pending = 0; } break;
                    case 0x0A: case 0x0B: case 0x0C: screen_newline(s); s->wraparound_pending = 0; break;
                    case 0x0D: s->cursor_x = 0; s->wraparound_pending = 0; break;
                    case 0x0E: break;
                    case 0x0F: break;
                }
            } else if (c == 0x7F) {
                // DEL - ignore
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
                execute_csi_internal(s, c, 0, s->param_buf, s->param_len, s->inter_buf, s->inter_len);
                s->state = ST_NORMAL;
            } else if (is_c0(c)) {
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
                execute_csi_internal(s, c, prefix, s->param_buf, s->param_len, s->inter_buf, s->inter_len);
                s->state = ST_NORMAL;
            } else if (is_c0(c)) {
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
                execute_csi_internal(s, c, prefix, s->param_buf, s->param_len, s->inter_buf, s->inter_len);
                s->state = ST_NORMAL;
            } else {
                s->state = ST_CSI_IGNORE;
            }
            break;

        case ST_CSI_IGNORE:
            if (is_final_byte(c)) s->state = ST_NORMAL;
            break;

        case ST_OSC_STRING:
            if (c == 0x07) {
                execute_osc(s);
                s->state = ST_NORMAL;
            } else if (c == 0x1B) {
                execute_osc(s);
                s->state = ST_ESC;
                s->param_len = 0;
                s->inter_len = 0;
            } else if (c >= 0x20 && c != 0x7F) {
                if (!s->osc_sep) {
                    if (c >= '0' && c <= '9') {
                        s->osc_num = (s->osc_num < 0 ? 0 : s->osc_num) * 10 + (c - '0');
                    } else if (c == ';') {
                        s->osc_sep = 1;
                    } else {
                        s->osc_sep = 1;
                    }
                } else {
                    if (s->osc_len < 511) s->osc_buf[s->osc_len++] = (char)c;
                }
            }
            break;

        case ST_DCS_ENTRY:
        case ST_DCS_PARAM:
        case ST_DCS_INTER:
        case ST_DCS_PASSTHROUGH:
        case ST_DCS_IGNORE:
        case ST_SOS_STRING:
            if (c == 0x07) s->state = ST_NORMAL;
            else if (c == 0x1B) { s->state = ST_ESC; s->param_len = 0; s->inter_len = 0; }
            break;
    }
}

void screen_process_output(ScreenBuffer *s, const char *data, int len) {
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];

        if (s->state == ST_NORMAL) {
            if (c >= 0xC0 && c < 0xFE) {
                if ((c & 0xE0) == 0xC0) {
                    if (c < 0xC2) continue;
                    s->utf8_cp = c & 0x1F; s->utf8_state = 1; continue;
                }
                else if ((c & 0xF0) == 0xE0) { s->utf8_cp = c & 0x0F; s->utf8_state = 2; continue; }
                else if ((c & 0xF8) == 0xF0) { s->utf8_cp = c & 0x07; s->utf8_state = 3; continue; }
                else continue;
            } else if (s->utf8_state == 0 && c >= 0xA0 && c < 0xC0) {
                continue;
            }
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
                s->utf8_state = 0;
            }
        }

        screen_process_byte(s, c);
    }
}
