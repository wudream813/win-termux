#include "screen.h"
#include "config.h"

/* ---------------------------------------------------------------------------
 * 一行的四个并行数组：cells / fg_rgb / bg_rgb / rgb_valid
 *
 * 它们必须永远一起分配、一起搬运、一起清空。历史上已经两次栽在「改了前三个、
 * 漏掉最后一个」上（v1.5.0 的 screen_scroll_up 漏拷 rgb_valid、v1.8.10 的
 * screen_resize alt 屏迁移只搬了每行第 0 列）。所有搬运统一收口到下面这几个
 * 辅助函数，以后再加第五个并行数组也只需要改这里。
 * ------------------------------------------------------------------------- */
static void line_free(ScreenLine *ln) {
    free(ln->cells);
    free(ln->fg_rgb);
    free(ln->bg_rgb);
    free(ln->rgb_valid);
    ln->cells = NULL;
    ln->fg_rgb = NULL;
    ln->bg_rgb = NULL;
    ln->rgb_valid = NULL;
}

/* 把一整行填成空白（不碰分配状态）。 */
static void line_fill_blank(ScreenLine *ln, int n, WORD attr) {
    if (!ln->cells) return;
    for (int j = 0; j < n; j++) {
        ln->cells[j].Char.UnicodeChar = L' ';
        ln->cells[j].Attributes = attr;
        if (ln->fg_rgb) ln->fg_rgb[j] = RGB565_WHITE;
        if (ln->bg_rgb) ln->bg_rgb[j] = RGB565_BLACK;
        if (ln->rgb_valid) ln->rgb_valid[j] = 0;
    }
}

/* 分配一行并填成空白；任一数组失败就整行回滚并返回 0。 */
static int line_alloc(ScreenLine *ln, int n, WORD attr) {
    ln->cells = (CHAR_INFO *)malloc(n * sizeof(CHAR_INFO));
    ln->fg_rgb = (WORD *)malloc(n * sizeof(WORD));
    ln->bg_rgb = (WORD *)malloc(n * sizeof(WORD));
    ln->rgb_valid = (unsigned char *)calloc(n, 1);
    if (!ln->cells || !ln->fg_rgb || !ln->bg_rgb || !ln->rgb_valid) {
        line_free(ln);
        return 0;
    }
    line_fill_blank(ln, n, attr);
    return 1;
}

/* 逐行搬运：四个数组各拷 n 个元素。 */
static void line_copy(ScreenLine *dst, const ScreenLine *src, int n) {
    if (!dst->cells || !src->cells || n <= 0) return;
    memcpy(dst->cells, src->cells, n * sizeof(CHAR_INFO));
    if (dst->fg_rgb && src->fg_rgb) memcpy(dst->fg_rgb, src->fg_rgb, n * sizeof(WORD));
    if (dst->bg_rgb && src->bg_rgb) memcpy(dst->bg_rgb, src->bg_rgb, n * sizeof(WORD));
    if (dst->rgb_valid && src->rgb_valid) memcpy(dst->rgb_valid, src->rgb_valid, n * sizeof(unsigned char));
}

/* alt 屏是扁平数组，按「行起点下标」搬运同样的四份数据。 */
static void alt_row_copy(CHAR_INFO *dc, WORD *dfr, WORD *dbr, unsigned char *dv, int di,
                         const CHAR_INFO *sc, const WORD *sfr, const WORD *sbr, const unsigned char *sv, int si,
                         int n) {
    if (n <= 0) return;
    if (dc && sc) memcpy(&dc[di], &sc[si], n * sizeof(CHAR_INFO));
    if (dfr && sfr) memcpy(&dfr[di], &sfr[si], n * sizeof(WORD));
    if (dbr && sbr) memcpy(&dbr[di], &sbr[si], n * sizeof(WORD));
    if (dv && sv) memcpy(&dv[di], &sv[si], n * sizeof(unsigned char));
}

int screen_ensure_line(ScreenBuffer *s, int pr) {
    if (!s->lines || pr < 0 || pr >= s->total_lines) return 0;
    if (s->lines[pr].cells) return 1;

    return line_alloc(&s->lines[pr], s->cols, s->current_attr ? s->current_attr : 0x07);
}

int screen_init(ScreenBuffer *s, int cols, int rows) {
    memset(s, 0, sizeof(*s));
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    s->cols = cols;
    s->rows = rows;
    s->total_lines = rows + g_scrollback_lines;
    s->current_attr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    s->fg_color = 7;
    s->bg_color = 0;

    s->lines = (ScreenLine *)calloc(s->total_lines, sizeof(ScreenLine));
    s->alt_buffer = (CHAR_INFO *)malloc(rows * cols * sizeof(CHAR_INFO));
    s->alt_fg_rgb = (WORD *)malloc(rows * cols * sizeof(WORD));
    s->alt_bg_rgb = (WORD *)malloc(rows * cols * sizeof(WORD));
    s->alt_rgb_valid = (unsigned char *)calloc(rows * cols, 1);

    if (!s->lines || !s->alt_buffer || !s->alt_fg_rgb || !s->alt_bg_rgb || !s->alt_rgb_valid) {
        screen_free(s);
        return 0;
    }

    for (int i = 0; i < rows * cols; i++) {
        s->alt_fg_rgb[i] = RGB565_WHITE;
        s->alt_bg_rgb[i] = RGB565_BLACK;
        s->alt_buffer[i].Char.UnicodeChar = L' ';
        s->alt_buffer[i].Attributes = s->current_attr;
    }

    // Allocate initial visible rows
    for (int r = 0; r < rows; r++) {
        screen_ensure_line(s, r);
    }

    s->scroll_top = 0;
    s->cursor_visible = 1;
    s->auto_wrap = 1;
    s->scroll_region_top = 0;
    s->scroll_region_bottom = rows - 1;

    for (int i = 0; i < cols && i < 512; i += 8)
        s->tab_stops[i] = 1;
    return 1;
}

void screen_free(ScreenBuffer *s) {
    if (s->lines) {
        for (int i = 0; i < s->total_lines; i++) line_free(&s->lines[i]);
        free(s->lines);
        s->lines = NULL;
    }
    free(s->alt_buffer); s->alt_buffer = NULL;
    free(s->alt_fg_rgb); s->alt_fg_rgb = NULL;
    free(s->alt_bg_rgb); s->alt_bg_rgb = NULL;
    free(s->alt_rgb_valid); s->alt_rgb_valid = NULL;
}

CHAR_INFO *screen_cell(ScreenBuffer *s, int row, int col) {
    if (row < 0 || col < 0 || col >= s->cols) return NULL;
    if (s->in_alt_screen) {
        if (row >= s->rows) return NULL;
        return &s->alt_buffer[row * s->cols + col];
    }
    if (row >= s->rows) return NULL;
    int pr = screen_phys_row(s, row);
    if (!screen_ensure_line(s, pr)) return NULL;
    return &s->lines[pr].cells[col];
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
    /* v1.8.15 修复：ctab[] 里存的是「前景」位标志（FOREGROUND_*，值 0..0xF），
     * 背景需要把同样的颜色位左移 4 位到 BACKGROUND_* 位置（如红 0x04<<4=0x40）。
     * 旧代码写成 (ctab[bg] >> 4) << 4——前景值右移 4 位恒为 0，导致所有走
     * 16/256 色 attr 的程序内容背景位永远是 0（黑背景）。colortool -c 的色块
     * 用的正是 ESC[48;5;Nm（量化进 attr），因此背景整片丢失。真彩（48;2;..）
     * 走 bg_rgb_on 分支不经这里，所以 win-termux 自身 UI 与真彩程序不受影响。 */
    WORD a = ctab[fg & 15] | (ctab[bg & 15] << 4);
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
        if (screen_ensure_line(s, pr)) {
            s->lines[pr].cells[col].Char.UnicodeChar = ch;
            s->lines[pr].cells[col].Attributes = attr;
            s->lines[pr].fg_rgb[col] = s->fg_rgb_on ? rgb565(s->fg_r, s->fg_g, s->fg_b) : RGB565_WHITE;
            s->lines[pr].bg_rgb[col] = s->bg_rgb_on ? rgb565(s->bg_r, s->bg_g, s->bg_b) : RGB565_BLACK;
            s->lines[pr].rgb_valid[col] = v;
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
            alt_row_copy(s->alt_buffer, s->alt_fg_rgb, s->alt_bg_rgb, s->alt_rgb_valid, i * s->cols,
                         s->alt_buffer, s->alt_fg_rgb, s->alt_bg_rgb, s->alt_rgb_valid, (i + count) * s->cols,
                         s->cols);
        }
        for (int i = bottom - count + 1; i <= bottom; i++)
            for (int j = 0; j < s->cols; j++)
                screen_write_cell(s, i, j, L' ', s->current_attr);
        return;
    }

    if (top == 0 && bottom == s->rows - 1) {
        int old_hist = s->hist_lines;
        s->hist_lines += count;
        int hist_cap = s->total_lines - s->rows;
        if (s->hist_lines > hist_cap) s->hist_lines = hist_cap;
        int dropped = (old_hist + count) - s->hist_lines;

        int pi = s->pane_index;
        if (pi >= 0 && pi < MAX_PANES && g_mux.panes[pi].active) {
            if (g_mux.panes[pi].scroll_offset > 0) {
                g_mux.panes[pi].scroll_offset += count;
                if (g_mux.panes[pi].scroll_offset > s->hist_lines)
                    g_mux.panes[pi].scroll_offset = s->hist_lines;
            }
            if (pi == g_mux.active_pane && g_search_active && g_search_match_count > 0 && dropped > 0) {
                int new_count = 0;
                int new_cur = -1;
                for (int m = 0; m < g_search_match_count; m++) {
                    g_search_matches[m].abs_y -= dropped;
                    if (g_search_matches[m].abs_y >= 0) {
                        if (m == g_search_match_cur) new_cur = new_count;
                        g_search_matches[new_count++] = g_search_matches[m];
                    }
                }
                g_search_match_count = new_count;
                /* BUG-9 (v1.8.11): 当前停留的匹配被滚出缓冲时，以前会跳到「最新」的
                 * 那一条，浏览位置从最老一端直接弹到最新一端。剔除总是从最老的一端
                 * 发生，所以存活项里的 index 0 恰好就是「原当前项之后最近的一条」。 */
                if (new_cur >= 0) {
                    g_search_match_cur = new_cur;
                } else if (new_count > 0) {
                    g_search_match_cur = 0;
                } else {
                    g_search_match_cur = -1;
                }
                if (g_search_match_count == 0) {
                    g_search_active = 0;
                }
            }
        }

        for (int c = 0; c < count; c++) {
            int pr = screen_phys_row(s, s->rows + c);
            if (s->lines && s->lines[pr].cells) {
                line_fill_blank(&s->lines[pr], s->cols, s->current_attr);
            }
        }
        s->scroll_top = (s->scroll_top + count) % s->total_lines;
    } else {
        // Partial scroll
        for (int i = top; i <= bottom - count; i++) {
            int dst_pr = screen_phys_row(s, i);
            int src_pr = screen_phys_row(s, i + count);
            if (s->lines && s->lines[src_pr].cells) {
                screen_ensure_line(s, dst_pr);
                line_copy(&s->lines[dst_pr], &s->lines[src_pr], s->cols);
            } else if (s->lines && s->lines[dst_pr].cells) {
                line_fill_blank(&s->lines[dst_pr], s->cols, s->current_attr);
            }
        }
        for (int i = bottom - count + 1; i <= bottom; i++) {
            int pr = screen_phys_row(s, i);
            if (s->lines && s->lines[pr].cells) {
                line_fill_blank(&s->lines[pr], s->cols, s->current_attr);
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
            alt_row_copy(s->alt_buffer, s->alt_fg_rgb, s->alt_bg_rgb, s->alt_rgb_valid, i * s->cols,
                         s->alt_buffer, s->alt_fg_rgb, s->alt_bg_rgb, s->alt_rgb_valid, (i - count) * s->cols,
                         s->cols);
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
            if (s->lines && s->lines[pr].cells) {
                line_fill_blank(&s->lines[pr], s->cols, s->current_attr);
            }
        }
    } else {
        for (int i = bottom; i >= top + count; i--) {
            int dst_pr = screen_phys_row(s, i);
            int src_pr = screen_phys_row(s, i - count);
            if (s->lines && s->lines[src_pr].cells) {
                screen_ensure_line(s, dst_pr);
                line_copy(&s->lines[dst_pr], &s->lines[src_pr], s->cols);
            } else if (s->lines && s->lines[dst_pr].cells) {
                line_fill_blank(&s->lines[dst_pr], s->cols, s->current_attr);
            }
        }
        for (int i = top; i < top + count && i <= bottom; i++) {
            int pr = screen_phys_row(s, i);
            if (s->lines && s->lines[pr].cells) {
                line_fill_blank(&s->lines[pr], s->cols, s->current_attr);
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
    if (nc < 1) nc = 1;
    if (nr < 1) nr = 1;
    int nt = nr + g_scrollback_lines;

    ScreenLine *nl = (ScreenLine *)calloc(nt, sizeof(ScreenLine));
    CHAR_INFO *na = (CHAR_INFO *)calloc(nr * nc, sizeof(CHAR_INFO));
    WORD *nafr = (WORD *)malloc(nr * nc * sizeof(WORD));
    WORD *nabr = (WORD *)malloc(nr * nc * sizeof(WORD));
    unsigned char *nav = (unsigned char *)calloc(nr * nc, 1);

    if (!nl || !na || !nafr || !nabr || !nav) {
        free(nl); free(na); free(nafr); free(nabr); free(nav);
        return 0;
    }
    for (int i = 0; i < nr * nc; i++) {
        nafr[i] = RGB565_WHITE; nabr[i] = RGB565_BLACK;
        na[i].Char.UnicodeChar = L' '; na[i].Attributes = s->current_attr ? s->current_attr : 0x07;
    }

    int cc = nc < s->cols ? nc : s->cols;
    int cr = nr < s->rows ? nr : s->rows;
    int nst = 0;

    int old_hist = s->hist_lines;
    int old_cap = s->total_lines - s->rows;
    if (old_hist > old_cap) old_hist = old_cap;
    if (old_hist > nt - nr) old_hist = nt - nr;

    // Migrate history lines that were allocated
    for (int h = 1; h <= old_hist; h++) {
        int old_r = screen_phys_row(s, -h);
        int new_r = (nst - h % nt + nt) % nt;
        if (old_r >= 0 && old_r < s->total_lines && s->lines && s->lines[old_r].cells) {
            if (line_alloc(&nl[new_r], nc, s->current_attr ? s->current_attr : 0x07))
                line_copy(&nl[new_r], &s->lines[old_r], cc);
        }
    }

    // Migrate visible lines
    for (int y = 0; y < cr; y++) {
        int new_r = (nst + y) % nt;
        int old_r = screen_phys_row(s, y);
        if (old_r >= 0 && old_r < s->total_lines && s->lines && s->lines[old_r].cells) {
            if (line_alloc(&nl[new_r], nc, s->current_attr ? s->current_attr : 0x07))
                line_copy(&nl[new_r], &s->lines[old_r], cc);
        } else {
            // Allocate blank visible line
            line_alloc(&nl[new_r], nc, s->current_attr ? s->current_attr : 0x07);
        }
    }

    // Migrate alt buffer
    /* BUG-8 (v1.8.11): 这里以前 rgb_valid 只搬了每行第 0 列（其余三个数组都搬了
     * cc 个），alt 屏一改窗口大小整屏真彩色就退化成 16 色。现在四个数组统一走
     * alt_row_copy()，不可能再漏。 */
    for (int y = 0; y < cr && y < s->rows; y++) {
        alt_row_copy(na, nafr, nabr, nav, y * nc,
                     s->alt_buffer, s->alt_fg_rgb, s->alt_bg_rgb, s->alt_rgb_valid, y * s->cols,
                     cc);
    }

    if (s->lines) {
        for (int i = 0; i < s->total_lines; i++) line_free(&s->lines[i]);
        free(s->lines);
    }
    free(s->alt_buffer); free(s->alt_fg_rgb); free(s->alt_bg_rgb); free(s->alt_rgb_valid);

    s->lines = nl;
    s->alt_buffer = na;
    s->alt_fg_rgb = nafr;
    s->alt_bg_rgb = nabr;
    s->alt_rgb_valid = nav;
    s->cols = nc;
    s->rows = nr;
    s->total_lines = nt;
    s->scroll_top = nst;
    s->hist_lines = old_hist;
    if (s->alt_hist_lines > nt - nr) s->alt_hist_lines = nt - nr;
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
    if (pr >= 0 && pr < s->total_lines && s->lines && s->lines[pr].cells) {
        unsigned char v = s->lines[pr].rgb_valid ? s->lines[pr].rgb_valid[col] : 0;
        *out_fv = (v & 1) ? 1 : 0;
        *out_bv = (v & 2) ? 1 : 0;
        *out_f = s->lines[pr].fg_rgb[col];
        *out_b = s->lines[pr].bg_rgb[col];
    }
}
