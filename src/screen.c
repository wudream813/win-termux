#include "screen.h"

int screen_init(ScreenBuffer *s, int cols, int rows) {
    memset(s, 0, sizeof(*s));
    s->cols = cols;
    s->rows = rows;
    s->total_lines = rows + SCROLL_BUF_LINES;
    s->buffer = (CHAR_INFO *)malloc(s->total_lines * cols * sizeof(CHAR_INFO));
    s->alt_buffer = (CHAR_INFO *)malloc(rows * cols * sizeof(CHAR_INFO));
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
        s->fg_rgb[i] = RGB565_WHITE;
        s->bg_rgb[i] = 0;
    }
    for (int i = 0; i < rows * cols; i++) {
        s->alt_fg_rgb[i] = RGB565_WHITE;
        s->alt_bg_rgb[i] = 0;
    }
    s->scroll_top = 0;
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

void screen_free(ScreenBuffer *s) {
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

CHAR_INFO *screen_cell(ScreenBuffer *s, int row, int col) {
    if (row < 0 || col < 0 || col >= s->cols) return NULL;
    if (s->in_alt_screen) {
        if (row >= s->rows) return NULL;
        return &s->alt_buffer[row * s->cols + col];
    }
    if (row >= s->rows) return NULL;
    int pr = screen_phys_row(s, row);
    return &s->buffer[pr * s->cols + col];
}

WORD build_attr(ScreenBuffer *s) {
    int fg = s->fg_color, bg = s->bg_color;
    if (s->reverse_video) { int t = fg; fg = bg; bg = t; }
    static const WORD ctab[16] = {
        0,
        FOREGROUND_RED,
        FOREGROUND_GREEN,
        FOREGROUND_RED | FOREGROUND_GREEN,
        FOREGROUND_BLUE,
        FOREGROUND_RED | FOREGROUND_BLUE,
        FOREGROUND_GREEN | FOREGROUND_BLUE,
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
        FOREGROUND_INTENSITY,
        FOREGROUND_RED | FOREGROUND_INTENSITY,
        FOREGROUND_GREEN | FOREGROUND_INTENSITY,
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
        FOREGROUND_BLUE | FOREGROUND_INTENSITY,
        FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
        FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY
    };
    WORD a = ctab[fg & 15] | ((ctab[bg & 15] >> 4) << 4);
    if (s->bold) a |= FOREGROUND_INTENSITY;
    if (s->underline) a |= COMMON_LVB_UNDERSCORE;
    return a;
}

void screen_write_cell(ScreenBuffer *s, int row, int col, WCHAR ch, WORD attr) {
    if (row < 0 || col < 0 || col >= s->cols) return;
    unsigned char v = (s->fg_rgb_on ? 1 : 0) | (s->bg_rgb_on ? 2 : 0);
    if (s->in_alt_screen) {
        if (row >= s->rows) return;
        s->alt_buffer[row * s->cols + col].Char.UnicodeChar = ch;
        s->alt_buffer[row * s->cols + col].Attributes = attr;
        if (s->alt_fg_rgb) {
            s->alt_fg_rgb[row * s->cols + col] = s->fg_rgb_on ? rgb565(s->fg_r, s->fg_g, s->fg_b) : RGB565_WHITE;
            s->alt_bg_rgb[row * s->cols + col] = s->bg_rgb_on ? rgb565(s->bg_r, s->bg_g, s->bg_b) : RGB565_BLACK;
            if (s->alt_rgb_valid) s->alt_rgb_valid[row * s->cols + col] = v;
        }
    } else {
        int pr = screen_phys_row(s, row);
        if (s->buffer) {
            s->buffer[pr * s->cols + col].Char.UnicodeChar = ch;
            s->buffer[pr * s->cols + col].Attributes = attr;
        }
        if (s->fg_rgb) {
            s->fg_rgb[pr * s->cols + col] = s->fg_rgb_on ? rgb565(s->fg_r, s->fg_g, s->fg_b) : RGB565_WHITE;
            s->bg_rgb[pr * s->cols + col] = s->bg_rgb_on ? rgb565(s->bg_r, s->bg_g, s->bg_b) : RGB565_BLACK;
            if (s->rgb_valid) s->rgb_valid[pr * s->cols + col] = v;
        }
    }
}

void screen_scroll_up(ScreenBuffer *s, int top, int bottom, int count) {
    if (count <= 0) return;
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
            if (s->alt_rgb_valid) {
                memcpy(&s->alt_rgb_valid[i * s->cols], &s->alt_rgb_valid[(i + count) * s->cols], s->cols * sizeof(unsigned char));
            }
        }
        for (int i = bottom - count + 1; i <= bottom; i++)
            for (int j = 0; j < s->cols; j++)
                screen_write_cell(s, i, j, L' ', s->current_attr);
        return;
    }
    if (top == 0 && bottom == s->rows - 1) {
        s->hist_lines += count;
        if (s->hist_lines > SCROLL_BUF_LINES) s->hist_lines = SCROLL_BUF_LINES;

        int pi = s->pane_index;
        if (pi >= 0 && pi < MAX_PANES && g_mux.panes[pi].active) {
            if (g_mux.panes[pi].scroll_offset > 0) {
                g_mux.panes[pi].scroll_offset += count;
                if (g_mux.panes[pi].scroll_offset > s->hist_lines)
                    g_mux.panes[pi].scroll_offset = s->hist_lines;
            }
        }

        for (int c = 0; c < count; c++) {
            int pr = screen_phys_row(s, s->rows + c);
            for (int j = 0; j < s->cols; j++) {
                int idx = pr * s->cols + j;
                s->buffer[idx].Char.UnicodeChar = L' ';
                s->buffer[idx].Attributes = s->current_attr;
                if (s->fg_rgb) { s->fg_rgb[idx] = RGB565_WHITE; s->bg_rgb[idx] = RGB565_BLACK; }
                if (s->rgb_valid) s->rgb_valid[idx] = 0;
            }
        }
        s->scroll_top = (s->scroll_top + count) % s->total_lines;
    } else {
        // v1.6.0 modulo wrapping fix
        for (int i = top; i <= bottom - count; i++) {
            int dst_pr = screen_phys_row(s, i);
            int src_pr = screen_phys_row(s, i + count);
            memcpy(&s->buffer[dst_pr * s->cols], &s->buffer[src_pr * s->cols], s->cols * sizeof(CHAR_INFO));
            if (s->fg_rgb) {
                memcpy(&s->fg_rgb[dst_pr * s->cols], &s->fg_rgb[src_pr * s->cols], s->cols * sizeof(WORD));
                memcpy(&s->bg_rgb[dst_pr * s->cols], &s->bg_rgb[src_pr * s->cols], s->cols * sizeof(WORD));
            }
            if (s->rgb_valid) {
                memcpy(&s->rgb_valid[dst_pr * s->cols], &s->rgb_valid[src_pr * s->cols], s->cols * sizeof(unsigned char));
            }
        }
        for (int i = bottom - count + 1; i <= bottom; i++) {
            int pr = screen_phys_row(s, i);
            for (int j = 0; j < s->cols; j++) {
                int idx = pr * s->cols + j;
                s->buffer[idx].Char.UnicodeChar = L' ';
                s->buffer[idx].Attributes = s->current_attr;
                if (s->fg_rgb) { s->fg_rgb[idx] = RGB565_WHITE; s->bg_rgb[idx] = RGB565_BLACK; }
                if (s->rgb_valid) s->rgb_valid[idx] = 0;
            }
        }
    }
}

void screen_scroll_down(ScreenBuffer *s, int top, int bottom, int count) {
    if (count <= 0) return;
    if (top < 0) top = 0;
    if (bottom >= s->rows) bottom = s->rows - 1;
    if (bottom < top) return;
    if (count > bottom - top + 1) count = bottom - top + 1;

    if (s->in_alt_screen) {
        for (int i = bottom; i >= top + count; i--) {
            memcpy(&s->alt_buffer[i * s->cols], &s->alt_buffer[(i - count) * s->cols], s->cols * sizeof(CHAR_INFO));
            if (s->alt_fg_rgb) {
                memcpy(&s->alt_fg_rgb[i * s->cols], &s->alt_fg_rgb[(i - count) * s->cols], s->cols * sizeof(WORD));
                memcpy(&s->alt_bg_rgb[i * s->cols], &s->alt_bg_rgb[(i - count) * s->cols], s->cols * sizeof(WORD));
            }
            if (s->alt_rgb_valid) {
                memcpy(&s->alt_rgb_valid[i * s->cols], &s->alt_rgb_valid[(i - count) * s->cols], s->cols * sizeof(unsigned char));
            }
        }
        for (int i = top; i < top + count && i <= bottom; i++) {
            for (int j = 0; j < s->cols; j++)
                screen_write_cell(s, i, j, L' ', s->current_attr);
        }
        return;
    }

    if (top == 0 && bottom == s->rows - 1) {
        s->hist_lines -= count;
        if (s->hist_lines < 0) s->hist_lines = 0;
        int pi = s->pane_index;
        if (pi >= 0 && pi < MAX_PANES && g_mux.panes[pi].active) {
            if (g_mux.panes[pi].scroll_offset > 0) {
                g_mux.panes[pi].scroll_offset -= count;
                if (g_mux.panes[pi].scroll_offset < 0)
                    g_mux.panes[pi].scroll_offset = 0;
            }
        }
        s->scroll_top = (s->scroll_top - count % s->total_lines + s->total_lines) % s->total_lines;
        for (int c = 0; c < count; c++) {
            int pr = screen_phys_row(s, c);
            for (int j = 0; j < s->cols; j++) {
                int idx = pr * s->cols + j;
                s->buffer[idx].Char.UnicodeChar = L' ';
                s->buffer[idx].Attributes = s->current_attr;
                if (s->fg_rgb) { s->fg_rgb[idx] = RGB565_WHITE; s->bg_rgb[idx] = RGB565_BLACK; }
                if (s->rgb_valid) s->rgb_valid[idx] = 0;
            }
        }
    } else {
        for (int i = bottom; i >= top + count; i--) {
            int dst_pr = screen_phys_row(s, i);
            int src_pr = screen_phys_row(s, i - count);
            memcpy(&s->buffer[dst_pr * s->cols], &s->buffer[src_pr * s->cols], s->cols * sizeof(CHAR_INFO));
            if (s->fg_rgb) {
                memcpy(&s->fg_rgb[dst_pr * s->cols], &s->fg_rgb[src_pr * s->cols], s->cols * sizeof(WORD));
                memcpy(&s->bg_rgb[dst_pr * s->cols], &s->bg_rgb[src_pr * s->cols], s->cols * sizeof(WORD));
            }
            if (s->rgb_valid) {
                memcpy(&s->rgb_valid[dst_pr * s->cols], &s->rgb_valid[src_pr * s->cols], s->cols * sizeof(unsigned char));
            }
        }
        for (int i = top; i < top + count && i <= bottom; i++) {
            int pr = screen_phys_row(s, i);
            for (int j = 0; j < s->cols; j++) {
                int idx = pr * s->cols + j;
                s->buffer[idx].Char.UnicodeChar = L' ';
                s->buffer[idx].Attributes = s->current_attr;
                if (s->fg_rgb) { s->fg_rgb[idx] = RGB565_WHITE; s->bg_rgb[idx] = RGB565_BLACK; }
                if (s->rgb_valid) s->rgb_valid[idx] = 0;
            }
        }
    }
}

void screen_newline(ScreenBuffer *s) {
    if (s->cursor_y >= s->scroll_region_bottom)
        screen_scroll_up(s, s->scroll_region_top, s->scroll_region_bottom, 1);
    else if (s->cursor_y < s->rows - 1)
        s->cursor_y++;
}

void detect_conpty_width(ScreenBuffer *s, int written_len) {
    (void)written_len;
    if (s->in_alt_screen) return;
    if (s->detect_count >= 5) return;
    if (s->cols >= 1000) return;

    if (s->cursor_y > 0 && s->cursor_x == 0) {
        int prev_r = s->cursor_y - 1;
        int last_char = -1;
        for (int c = s->cols - 1; c >= 0; c--) {
            CHAR_INFO *ci = screen_cell(s, prev_r, c);
            if (ci && ci->Char.UnicodeChar != L' ') {
                last_char = c;
                break;
            }
        }
        if (last_char >= s->cols - 1) {
            s->detect_count++;
            if (s->detect_count >= 3) {
                int new_cols = s->cols + 8;
                if (new_cols <= 500) {
                    screen_resize(s, new_cols, s->rows);
                    s->detect_count = 0;
                }
            }
        }
    }
}

int screen_resize(ScreenBuffer *s, int nc, int nr) {
    if (nc == s->cols && nr == s->rows) return 1;
    int nt = nr + SCROLL_BUF_LINES;
    CHAR_INFO *nb = (CHAR_INFO *)malloc(nt * nc * sizeof(CHAR_INFO));
    CHAR_INFO *na = (CHAR_INFO *)malloc(nr * nc * sizeof(CHAR_INFO));
    WORD *nfr = (WORD *)malloc(nt * nc * sizeof(WORD));
    WORD *nbr = (WORD *)malloc(nt * nc * sizeof(WORD));
    WORD *nafr = (WORD *)malloc(nr * nc * sizeof(WORD));
    WORD *nabr = (WORD *)malloc(nr * nc * sizeof(WORD));
    unsigned char *nrv = (unsigned char *)calloc(nt * nc, 1);
    unsigned char *nav = (unsigned char *)calloc(nr * nc, 1);
    if (!nb || !na || !nfr || !nbr || !nafr || !nabr || !nrv || !nav) {
        free(nb); free(na); free(nfr); free(nbr); free(nafr); free(nabr); free(nrv); free(nav);
        return 0;
    }
    for (int i = 0; i < nt * nc; i++) {
        nb[i].Char.UnicodeChar = L' '; nb[i].Attributes = s->current_attr;
        nfr[i] = RGB565_WHITE; nbr[i] = 0;
    }
    for (int i = 0; i < nr * nc; i++) {
        na[i].Char.UnicodeChar = L' '; na[i].Attributes = s->current_attr;
        nafr[i] = RGB565_WHITE; nabr[i] = 0;
    }
    int cc = nc < s->cols ? nc : s->cols;
    int cr = nr < s->rows ? nr : s->rows;
    int nst = 0;

    int old_hist = s->hist_lines;
    if (old_hist > SCROLL_BUF_LINES) old_hist = SCROLL_BUF_LINES;
    for (int h = 1; h <= old_hist; h++) {
        int old_r = screen_phys_row(s, -h);
        int new_r = nst - h;
        if (old_r >= 0 && old_r < s->total_lines && new_r >= 0 && new_r < nt) {
            for (int x = 0; x < cc; x++) {
                int old_idx = old_r * s->cols + x;
                int new_idx = new_r * nc + x;
                nb[new_idx] = s->buffer[old_idx];
                if (s->fg_rgb) { nfr[new_idx] = s->fg_rgb[old_idx]; nbr[new_idx] = s->bg_rgb[old_idx]; }
                if (s->rgb_valid) nrv[new_idx] = s->rgb_valid[old_idx];
            }
        }
    }

    for (int y = 0; y < cr; y++) {
        int new_r = nst + y;
        int old_r = screen_phys_row(s, y);
        for (int x = 0; x < cc; x++) {
            CHAR_INFO *src = screen_cell(s, y, x);
            if (src) nb[new_r * nc + x] = *src;
            WORD fv = RGB565_WHITE, bv = RGB565_BLACK; unsigned char vv = 0;
            if (old_r >= 0 && old_r < s->total_lines && s->fg_rgb) {
                fv = s->fg_rgb[old_r * s->cols + x];
                bv = s->bg_rgb[old_r * s->cols + x];
                if (s->rgb_valid) vv = s->rgb_valid[old_r * s->cols + x];
            }
            nfr[new_r * nc + x] = fv; nbr[new_r * nc + x] = bv; nrv[new_r * nc + x] = vv;
        }
    }

    for (int y = 0; y < cr && y < s->rows; y++) {
        for (int x = 0; x < cc; x++) {
            na[y * nc + x] = s->alt_buffer[y * s->cols + x];
            if (s->alt_fg_rgb) { nafr[y * nc + x] = s->alt_fg_rgb[y * s->cols + x]; nabr[y * nc + x] = s->alt_bg_rgb[y * s->cols + x]; }
            if (s->alt_rgb_valid) nav[y * nc + x] = s->alt_rgb_valid[y * s->cols + x];
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
    s->wraparound_pending = 0;
    memset(s->tab_stops, 0, sizeof(s->tab_stops));
    for (int i = 0; i < nc && i < 512; i += 8) s->tab_stops[i] = 1;
    return 1;
}

void cell_truecolor(ScreenBuffer *s, int row, int col, int ar, WORD *out_f, WORD *out_b, int *out_fv, int *out_bv) {
    *out_f = RGB565_WHITE; *out_b = RGB565_BLACK;
    *out_fv = 0; *out_bv = 0;
    if (row < 0 || col < 0 || col >= s->cols) return;
    if (s->in_alt_screen) {
        if (row >= s->rows || !s->alt_fg_rgb) return;
        unsigned char v = s->alt_rgb_valid ? s->alt_rgb_valid[row * s->cols + col] : 0;
        *out_fv = (v & 1) ? 1 : 0;
        *out_bv = (v & 2) ? 1 : 0;
        if (*out_fv || *out_bv) {
            *out_f = s->alt_fg_rgb[row * s->cols + col];
            *out_b = s->alt_bg_rgb[row * s->cols + col];
        }
        return;
    }
    int pr = (ar >= 0) ? ar : screen_phys_row(s, row);
    if (pr >= 0 && pr < s->total_lines && s->fg_rgb) {
        unsigned char v = s->rgb_valid ? s->rgb_valid[pr * s->cols + col] : 0;
        *out_fv = (v & 1) ? 1 : 0;
        *out_bv = (v & 2) ? 1 : 0;
        *out_f = s->fg_rgb[pr * s->cols + col];
        *out_b = s->bg_rgb[pr * s->cols + col];
    }
}
