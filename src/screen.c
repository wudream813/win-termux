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
    ln->len = 0;
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
    ln->len = n;
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
    s->line_wrap = (unsigned char *)calloc(s->total_lines, 1);
    s->alt_buffer = (CHAR_INFO *)malloc(rows * cols * sizeof(CHAR_INFO));
    s->alt_fg_rgb = (WORD *)malloc(rows * cols * sizeof(WORD));
    s->alt_bg_rgb = (WORD *)malloc(rows * cols * sizeof(WORD));
    s->alt_rgb_valid = (unsigned char *)calloc(rows * cols, 1);

    if (!s->lines || !s->line_wrap || !s->alt_buffer || !s->alt_fg_rgb || !s->alt_bg_rgb || !s->alt_rgb_valid) {
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
    free(s->line_wrap); s->line_wrap = NULL;
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
            /* 该物理槽进入可见区作新行，软换行续行标志清零（它将是硬换行新行，
             * 直到屏幕 put 逻辑再次自动折行时由 screen_mark_softwrap 标记）。 */
            if (s->line_wrap) s->line_wrap[pr] = 0;
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

/* 标记「光标所在物理行」为上一行的软换行续行（自动折行折下来的，不是硬换行）。
 * v1.8.47：历史 reflow 据此把相邻物理行合并回同一条逻辑行。在 vt.c 的自动折行
 * 路径（wraparound_pending 触发的 newline）调用；普通回车 / LF / IND 不调用。 */
void screen_mark_softwrap(ScreenBuffer *s) {
    if (!s->line_wrap || s->in_alt_screen) return;
    int pr = screen_phys_row(s, s->cursor_y);
    if (pr >= 0 && pr < s->total_lines) s->line_wrap[pr] = 1;
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
    unsigned char *nwrap = (unsigned char *)calloc(nt, 1);   /* v1.8.47 软换行标志 */
    CHAR_INFO *na = (CHAR_INFO *)calloc(nr * nc, sizeof(CHAR_INFO));
    WORD *nafr = (WORD *)malloc(nr * nc * sizeof(WORD));
    WORD *nabr = (WORD *)malloc(nr * nc * sizeof(WORD));
    unsigned char *nav = (unsigned char *)calloc(nr * nc, 1);

    if (!nl || !nwrap || !na || !nafr || !nabr || !nav) {
        free(nl); free(nwrap); free(na); free(nafr); free(nabr); free(nav);
        return 0;
    }
    for (int i = 0; i < nr * nc; i++) {
        nafr[i] = RGB565_WHITE; nabr[i] = RGB565_BLACK;
        na[i].Char.UnicodeChar = L' '; na[i].Attributes = s->current_attr ? s->current_attr : 0x07;
    }

    int cc = nc < s->cols ? nc : s->cols;
    int cr = nr < s->rows ? nr : s->rows;
    int nst = 0;
    /* v1.8.45 #6：历史行可能比新窗格宽（收窄后保留旧宽内容），其实际宽度存在
     * ScreenLine.len；见下方历史迁移。可见行按新宽 nc，ConPTY resize 后会重排。 */

    int old_hist = s->hist_lines;
    int old_cap = s->total_lines - s->rows;
    if (old_hist > old_cap) old_hist = old_cap;
    if (old_hist > nt - nr) old_hist = nt - nr;

    /* ---- 行迁移（底部锚定，历史不随 resize 变动）----
     * resize 只改变可见窗口大小，绝不新增/删除历史——历史只由真实滚动产生。
     * 可见区内容【底部对齐】：命令提示符（屏幕底部）始终留在底部。
     * 锚点偏移 k = nr - rows（缩小 k<0、放大 k>0、纯宽变 k=0）：
     *   新可见行 y 的旧逻辑行 src = y - k（src>=0 为旧可见行、src<0 为旧历史）。
     *  - 缩小（k<0）：y=0 取旧可见 -k 行，被裁的顶部 |k| 行【不滚入历史】——
     *    ConPTY resize 后会按新高度重新输出可见内容，把可见行移入历史会与
     *    ConPTY 的重发重复（v1.8.45 修复：上下分屏调大小后历史重复）。
     *  - 放大（k>0）：底部 rows 行取旧可见行，顶部 k 行从旧历史最新行回补
     *    （src<0），历史内容不变、只是暂时显示在可见区。
     * 历史行数恒为 min(old_hist, newcap)。 */
    WORD fill_attr = s->current_attr ? s->current_attr : 0x07;
    int newcap = nt - nr;
    int k = nr - s->rows;                          /* 锚点偏移：缩小<0、放大>0 */
    int new_hist = old_hist;
    if (new_hist > newcap) new_hist = newcap;
    if (new_hist < 0) new_hist = 0;

    /* 取「旧布局逻辑行 src」的物理行：src>=0 为旧可见行、src<0 为旧历史第 -src 新；
     * 越界/不存在返回 -1。 */
    #define RESIZE_OLD_ROW(src) ( \
        ((src) >= 0 && (src) < s->rows) ? screen_phys_row(s, (src)) : \
        ((src) < 0 && -(src) >= 1 && -(src) <= old_hist) ? screen_phys_row(s, (src)) : -1)

    /* 迁移新可见区（rel 0..nr-1）：src = y - k（底部锚定）。src<0 时从旧历史
     * 回补（仅放大会发生），取不到则空白。 */
    for (int y = 0; y < nr; y++) {
        int new_r = (nst + y) % nt;
        int src = y - k;
        int old_r = RESIZE_OLD_ROW(src);
        if (old_r >= 0 && old_r < s->total_lines && s->lines && s->lines[old_r].cells) {
            if (line_alloc(&nl[new_r], nc, fill_attr))
                line_copy(&nl[new_r], &s->lines[old_r], cc);
        } else {
            line_alloc(&nl[new_r], nc, fill_attr);
        }
    }
    /* 迁移新历史区（rel -1..-new_hist，-1 最新）：resize 不改历史，新历史 -h
     * 恒对应旧历史 -h（同一行，只改宽度）。被裁的可见行不滚入历史（见上）。 */
    for (int h = 1; h <= new_hist; h++) {
        /* 新历史第 h 行（-1 最新）位于新可见首行 nst 的「上 h 行」：
         * (nst - h) 环回。历史在循环缓冲里排在可见区之前；注意必须先减再取模
         * （- 优先级高于 % 会错算成 nst-(h%nt)）。 */
        int new_r = ((nst - h) % nt + nt) % nt;
        int old_r = (h <= old_hist) ? screen_phys_row(s, -h) : -1;
        if (old_r >= 0 && old_r < s->total_lines && s->lines && s->lines[old_r].cells) {
            /* v1.8.45 #6：历史行完整保留——目标行宽度取 max(新窗格宽 nc, 源行
             * 实际 len)。收窄时源历史行 len=旧宽 > nc，整行右侧内容（命令/输出）
             * 全部保留，不截断；之后拖宽回来即可完整显示。 */
            int src_len = s->lines[old_r].len > 0 ? s->lines[old_r].len : s->cols;
            int dst_w = nc > src_len ? nc : src_len;
            if (line_alloc(&nl[new_r], dst_w, fill_attr))
                line_copy(&nl[new_r], &s->lines[old_r], src_len);
            if (s->line_wrap) nwrap[new_r] = s->line_wrap[old_r];  /* 续行关系随行走 */
        }
    }
    /* 可见区迁移：同样把旧物理行的续行标志带过来（src>=0 旧可见、src<0 旧历史
     * 回补）。 */
    for (int y = 0; y < nr; y++) {
        int new_r = (nst + y) % nt;
        int src = y - k;
        int old_r = RESIZE_OLD_ROW(src);
        if (old_r >= 0 && s->line_wrap) nwrap[new_r] = s->line_wrap[old_r];
    }
    #undef RESIZE_OLD_ROW
    (void)cr;

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
    free(s->line_wrap);
    free(s->alt_buffer); free(s->alt_fg_rgb); free(s->alt_bg_rgb); free(s->alt_rgb_valid);

    s->lines = nl;
    s->line_wrap = nwrap;
    s->alt_buffer = na;
    s->alt_fg_rgb = nafr;
    s->alt_bg_rgb = nabr;
    s->alt_rgb_valid = nav;
    s->cols = nc;
    s->rows = nr;
    s->total_lines = nt;
    /* 尺寸扩大（拖分屏条 / 拉大窗口）时，新扩出的列默认是「无真彩、纯黑底」空白，
     * 而窗格内 shell 已绘制区域是 ConPTY 的真彩色背景（深色但非纯黑），两者交界会
     * 显出一条颜色不同的带（拖条时「右侧多一块背景」）。标准终端 resize 会把旧行
     * 右缘的背景向右延展，因此这里把每行已迁移部分最右格的属性/前景/背景真彩复制
     * 给新增列（字符保持空格），新增行没有可继承的旧格则维持默认。 */
    if (nc > cc) {
        for (int idx = 0; idx < nt; idx++) {
            ScreenLine *ln = &nl[idx];
            if (!ln->cells || ln->len < nc) continue;
            /* 背景延展只填补「旧窗口宽度之外」的新列（x 从旧宽 cc 起）。但历史
             * 宽行（v1.8.45 #6 保留了旧宽内容，len 可能来自更宽的历史）其 cc..nc-1
             * 列已是迁移过来的真实内容，绝不能填空白覆盖——这种行整体跳过：它的
             * 内容列本就填满，无需延展。判据：该行是从更宽源迁移来的历史行。 */
            int is_wide_hist = 0;
            /* 历史区物理行：新可见首行 nst 之上（环回）。简单判据——宽行的 len
             * 大于本次新窗格宽 nc 时必然含旧宽内容；这里 len>=nc 且来自历史则跳过。
             * 为稳妥，仅对【可见区】行做延展（可见行宽度恒=nc 且 cc 左侧为真实内容，
             * cc 右侧是新空列）；历史行 len>=nc 说明是保留的宽行，整行跳过。 */
            int phys = idx;
            int rel = (phys - nst % nt + nt) % nt;   /* 相对新可见首行：0..nr-1 可见 */
            if (rel >= nr) is_wide_hist = 1;          /* 环回区 = 历史 */
            if (is_wide_hist) continue;
            int src = cc - 1;
            for (int x = cc; x < nc; x++) {
                ln->cells[x].Char.UnicodeChar = L' ';
                ln->cells[x].Attributes = ln->cells[src].Attributes;
                if (ln->fg_rgb) ln->fg_rgb[x] = ln->fg_rgb[src];
                if (ln->bg_rgb) ln->bg_rgb[x] = ln->bg_rgb[src];
                if (ln->rgb_valid) ln->rgb_valid[x] = ln->rgb_valid[src];
            }
        }
    }
    s->scroll_top = nst;
    s->hist_lines = new_hist;
    if (s->alt_hist_lines > nt - nr) s->alt_hist_lines = nt - nr;
    if (s->cursor_x >= nc) s->cursor_x = nc - 1;
    if (s->cursor_y >= nr) s->cursor_y = nr - 1;
    s->scroll_region_top = 0; s->scroll_region_bottom = nr - 1;
    s->wraparound_pending = 0;
    memset(s->tab_stops, 0, sizeof(s->tab_stops));
    for (int i = 0; i < nc && i < 512; i += 8) s->tab_stops[i] = 1;
    return 1;
}

/* 宽字符在屏幕缓冲里占两个相邻 CHAR_INFO 单元格。这些吸附/步进函数按【单元格
 * 列号】索引，必须用 CHAR_INFO*（步长 = sizeof(CHAR_INFO)，含 Char+Attributes），
 * 而不能取 &cells[0].Char.UnicodeChar 当 WCHAR*——那样按 sizeof(WCHAR) 步长索引，
 * 会读到相邻单元格的 Attributes，列号全错位（历史 bug：→ 要按两下才跨过汉字）。
 * LCC() 取一行里第 i 个单元格的字符。 */
#define LCC(line, i) ((line)[(i)].Char.UnicodeChar)

/* 选区边界「吸附到完整字符」。宽字符（中文/全角/宽符号占两列；emoji 等
 * non-BMP 字符以高/低代理对占两格）在屏幕上跨两列，选区端点若正好落在它
 * 中间，会出现「选中半个字」——高亮只盖一半、复制结果也容易错位。
 * 规则（只看一行的单元格缓冲，便于纯函数回归）：
 *   - 右端点在某个宽字符的【主格】上（BMP 宽字符本格、或 emoji 高代理），
 *     右扩 1 格把次格也纳入；
 *   - 左端点落在某个宽字符的【次格】上（BMP 宽字符的占位 0、或 emoji 低代理），
 *     左退 1 格把主格也纳入。
 * 返回调整后的端点，夹紧到 [0, ncols-1]。 */
int snap_right_to_char(const CHAR_INFO *line, int ncols, int x) {
    if (x < 0 || !line) return x;
    if (x < ncols) {
        WCHAR c = LCC(line, x);
        int wide_lead =
            ((c >= 0xD800 && c <= 0xDBFF) && x + 1 < ncols &&
             LCC(line, x + 1) >= 0xDC00 && LCC(line, x + 1) <= 0xDFFF) ? 1
            : (!(c >= 0xD800 && c <= 0xDFFF) && is_wide_cp((unsigned int)c));
        if (wide_lead && x + 1 < ncols) return x + 1;
    }
    return x;
}

int snap_left_to_char(const CHAR_INFO *line, int ncols, int x) {
    (void)ncols;
    if (x <= 0 || !line) return x;
    WCHAR c = LCC(line, x);
    /* emoji 低代理：主格在 x-1（高代理） */
    if (c >= 0xDC00 && c <= 0xDFFF) {
        if (x - 1 >= 0 && LCC(line, x - 1) >= 0xD800 && LCC(line, x - 1) <= 0xDBFF)
            return x - 1;
        return x;
    }
    /* BMP 宽字符的占位格：本格 ch==0、左邻是宽字符主格 */
    if (c == 0) {
        WCHAR prev = LCC(line, x - 1);
        if (!(prev >= 0xD800 && prev <= 0xDBFF) && is_wide_cp((unsigned int)prev))
            return x - 1;
    }
    return x;
}

/* 单元格 k 是否为宽字符的【次格】（占位）：BMP 宽字符写 0、emoji 写低代理。
 * lead[out]（非 NULL）返回该宽字符主格列号。 */
static int cell_is_wide_trail(const CHAR_INFO *line, int k, int *lead) {
    if (k < 1) return 0;
    WCHAR c = LCC(line, k);
    if (c >= 0xDC00 && c <= 0xDFFF &&
        LCC(line, k - 1) >= 0xD800 && LCC(line, k - 1) <= 0xDBFF) {
        if (lead) *lead = k - 1;
        return 1;
    }
    if (c == 0) {
        WCHAR p = LCC(line, k - 1);
        if (!(p >= 0xD800 && p <= 0xDBFF) && is_wide_cp((unsigned int)p)) {
            if (lead) *lead = k - 1;
            return 1;
        }
    }
    return 0;
}

/* 复制模式里按字符移动光标：一次跨过整个宽字符（中文/全角/emoji），光标永远
 * 停在【主格】上，不停在次格（占位）。dir=+1 向右、-1 向左；line 为该行单元格。
 * 规则：右移 = 右侧下一个主格（宽字符主格跨 2 列）；左移 = 左边最近的主格
 * （左邻若为次格，则该宽字符主格在再左一列）。 */
int copy_step_char(const CHAR_INFO *line, int ncols, int x, int dir) {
    if (!line || dir == 0) return x;
    if (x < 0) x = 0;
    if (dir > 0) {
        if (x >= ncols) return ncols - 1;
        WCHAR c = LCC(line, x);
        int emoji_lead = (c >= 0xD800 && c <= 0xDBFF && x + 1 < ncols &&
                          LCC(line, x + 1) >= 0xDC00 && LCC(line, x + 1) <= 0xDFFF);
        int wide_lead  = !(c >= 0xD800 && c <= 0xDFFF) && is_wide_cp((unsigned int)c);
        int step = (emoji_lead || wide_lead) ? 2 : 1;
        int nx = x + step;
        return nx < ncols ? nx : ncols - 1;
    } else {
        if (x <= 0) return 0;
        int lead;
        if (cell_is_wide_trail(line, x - 1, &lead))
            return lead;          /* 左邻是次格 -> 落到它的宽字符主格 */
        return x - 1;             /* 左邻本身就是主格（窄字符/空格/宽主格） */
    }
}

/* 复制模式里把【光标所在列】整字化：无论鼠标点选/拖动还是上下移动，光标都不
 * 许停在宽字符的次格（半个汉字/全角/emoji）上。若 x 落在次格，则退到它所属宽
 * 字符的主格；落在主格或窄字符上则原样返回。返回值夹紧到 [0, ncols-1]。 */
int copy_cursor_to_lead(const CHAR_INFO *line, int ncols, int x) {
    if (!line) return x;
    if (x < 0) return 0;
    if (x >= ncols) return ncols > 0 ? ncols - 1 : 0;
    int lead;
    if (cell_is_wide_trail(line, x, &lead)) return lead;
    return x;
}

void cell_truecolor(ScreenBuffer *s, int row, int col, int ar, WORD *out_f, WORD *out_b, int *out_fv, int *out_bv) {    *out_f = RGB565_WHITE; *out_b = RGB565_BLACK;
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

/* ---- v1.8.47：滚动历史的逻辑行 reflow 视图 --------------------------------
 * 物理环形缓冲里每物理行可能是「上一行自动折行折下来的续行」（line_wrap==1）。
 * 渲染滚动历史时，把相邻物理行按 wrap 标志合并回逻辑行，再按【当前视口宽】重新
 * 折行：窄视口把一条长逻辑行折成多行完整显示、拖宽折回一行，内容不丢不重。
 *
 * 实现（正序、追加式，避免倒序/游标底填的行号错误）：
 *   1) 自老→新扫描物理行（最老历史 -hist_lines 到最新可见 rows-1），按 line_wrap
 *      把续行并入当前逻辑行（段内正序、段间尾接），硬换行结束一条逻辑行；
 *   2) 每条逻辑行按视口宽宽字符感知折行，折出的显示行【正向追加】到动态数组
 *      vrows（老在上、新在下）；
 *   3) 向上回看 vo：视口显示显示行 [total-vo-rows .. total-vo-1]（total-vo-1 为
 *      视口底部 = 距今 vo 个显示行之前的那行），拷入 out[y][x]。
 * 只扫描到「视口底部对应逻辑行」即可停止（更老内容不入视口），时间与视口大小
 * 线性相关、与总历史量无关。 */

static int reflow_glyph_w(WCHAR ch) {
    if (ch >= 0xDC00 && ch <= 0xDFFF) return 0;   /* emoji 低代理（次格） */
    if (ch >= 0xD800 && ch <= 0xDBFF) return 2;   /* emoji 高代理（主格） */
    if (ch == 0) return 0;                          /* BMP 宽字符次格占位 */
    return is_wide_cp((unsigned int)ch) ? 2 : 1;
}

/* 把一条逻辑行 g[0..n-1]（老→新）按宽 w 折行，折出的每条显示行【正向】写入 cb
 * （cb 收到该显示行的 RGlyph 段，返回非 0 表示停止）。空行硬放避免超宽 glyph
 * 死循环。 */
static int reflow_append_rows(const RGlyph *g, int n, int w,
                              int (*cb)(const RGlyph *line, int cnt, void *ud),
                              void *ud) {
    if (w < 1) w = 1;
    int col = 0, seg_start = 0;
    int k = 0;
    while (k < n) {
        WCHAR ch = g[k].ci.Char.UnicodeChar;
        int gw = reflow_glyph_w(ch);
        int adv = (gw == 2 && k + 1 < n) ? 2 : 1;
        if (gw > 0 && col > 0 && col + gw > w) {
            /* 当前显示行放不下（且行非空）：先把 seg_start..k-1 作为一条显示行发出 */
            if (cb(g + seg_start, k - seg_start, ud)) return 1;
            seg_start = k;
            col = 0;
        }
        if (gw > 0) col += gw;
        k += adv;
    }
    /* 末段（含空逻辑行 n==0 时发一条空行）。 */
    if (n == 0) {
        RGlyph blank; blank.ci.Char.UnicodeChar = L' '; blank.ci.Attributes = 0x07;
        blank.fg = RGB565_WHITE; blank.bg = RGB565_BLACK; blank.v = 0;
        if (cb(&blank, 0, ud)) return 1;
    } else if (cb(g + seg_start, n - seg_start, ud)) {
        return 1;
    }
    return 0;
}

/* cb 上下文：把每条显示行追加到动态数组（行主序，每行 width 列）。 */
typedef struct {
    RGlyph *buf;
    int cap;        /* 已分配【行数】 */
    int count;      /* 已追加行数 */
    int width;
} RfAcc;

static int reflow_acc_cb(const RGlyph *line, int cnt, void *ud) {
    RfAcc *a = (RfAcc *)ud;
    if (a->count >= a->cap) {
        int ncap = a->cap ? a->cap * 2 : 64;
        RGlyph *nb = (RGlyph *)realloc(a->buf, (size_t)ncap * a->width * sizeof(RGlyph));
        if (!nb) return 1;
        a->buf = nb; a->cap = ncap;
    }
    RGlyph *dst = a->buf + (size_t)a->count * a->width;
    for (int x = 0; x < a->width; x++) {
        dst[x].ci.Char.UnicodeChar = L' ';
        dst[x].ci.Attributes = 0x07;
        dst[x].fg = RGB565_WHITE; dst[x].bg = RGB565_BLACK; dst[x].v = 0;
    }
    int m = cnt < a->width ? cnt : a->width;
    for (int x = 0; x < m; x++) dst[x] = line[x];
    a->count++;
    return 0;
}

int screen_reflow_view(ScreenBuffer *s, int vo, int rows, int width, RGlyph *out) {
    if (!s || !s->line_wrap || s->in_alt_screen || rows <= 0 || width <= 0 || !out) return 0;

    /* 输出先清成空白。 */
    for (int i = 0; i < rows * width; i++) {
        out[i].ci.Char.UnicodeChar = L' '; out[i].ci.Attributes = 0x07;
        out[i].fg = RGB565_WHITE; out[i].bg = RGB565_BLACK; out[i].v = 0;
    }

    RfAcc acc; acc.buf = NULL; acc.cap = 0; acc.count = 0; acc.width = width;

    /* 逻辑行累积缓冲（老→新）。自老→新扫描物理行，续行直接【尾接】当前逻辑行。 */
    int logcap = width > 256 ? width : 256;
    RGlyph *logrow = (RGlyph *)malloc((size_t)logcap * sizeof(RGlyph));
    if (!logrow) return 0;
    int logn = 0;
    int have_log = 0;

    /* 物理行范围：【只扫描历史】-hist_lines .. -1（-1 = 最新历史行，即滚出可见区
     * 的最底行）。可见区实时行（rel 0..rows-1）由 ConPTY 正常路径渲染，绝不能
     * 折进来——否则 vo=0 时会把当前命令行/实时内容当成历史显示在顶部，且 vo>0
     * 时与实时屏重复（v1.8.47「最上面行故障」根因）。 */
    int first_rel = -s->hist_lines;
    int last_rel  = -1;

    for (int rel = first_rel; rel <= last_rel; rel++) {
        int pr = screen_phys_row(s, rel);
        if (pr < 0 || pr >= s->total_lines || !s->lines || !s->lines[pr].cells) continue;
        int len = s->lines[pr].len;
        /* 去物理行尾空格填充（次格 ch==0 不算空格）。 */
        while (len > 0 && s->lines[pr].cells[len - 1].Char.UnicodeChar == L' ') len--;
        int wrap = s->line_wrap[pr] ? 1 : 0;

        /* wrap=1（续行）且已有逻辑行：尾接本行。否则（硬换行/首行）先结束上一条
         * 逻辑行，再开新逻辑行。 */
        if (!wrap && have_log) {
            reflow_append_rows(logrow, logn, width, reflow_acc_cb, &acc);
            logn = 0;
        }
        have_log = 1;
        if (logn + len > logcap) {
            while (logn + len > logcap) logcap *= 2;
            RGlyph *nl = (RGlyph *)realloc(logrow, (size_t)logcap * sizeof(RGlyph));
            if (!nl) { free(logrow); free(acc.buf); return 0; }
            logrow = nl;
        }
        for (int x = 0; x < len; x++) {
            RGlyph g;
            g.ci = s->lines[pr].cells[x];
            g.fg = s->lines[pr].fg_rgb ? s->lines[pr].fg_rgb[x] : RGB565_WHITE;
            g.bg = s->lines[pr].bg_rgb ? s->lines[pr].bg_rgb[x] : RGB565_BLACK;
            g.v  = s->lines[pr].rgb_valid ? s->lines[pr].rgb_valid[x] : 0;
            logrow[logn++] = g;
        }
        /* 续行尾接完成后不结束逻辑行；硬换行已在循环顶结束。这里 wrap=0 时本行是
         * 新逻辑行首段，保持开启。 */
    }
    if (have_log) reflow_append_rows(logrow, logn, width, reflow_acc_cb, &acc);
    free(logrow);

    /* 视口窗口：显示行 acc.count 条（0=最老，count-1=最新历史行）。向上回看 vo，
     * 视口能看到的历史显示行 = [acc.count-vo-rows .. acc.count-1-vo] 中存在的段；
     * 把这些历史行【顶对齐】填入 out（y=0 起连续），返回历史行数 n。调用侧只对
     * 顶部 n 行用 reflow，其下 rows-n 行回落到实时 ConPTY 缓冲（实时屏）。 */
    int start = acc.count - vo - rows;   /* 视口最顶行对应的显示行号（可能<0） */
    if (start < 0) start = 0;
    int end = acc.count - 1 - vo;        /* 视口最底行对应的显示行号 */
    int n = 0;
    for (int src = start; src <= end && n < rows; src++, n++) {
        if (src < 0 || src >= acc.count) { n--; break; }
        for (int x = 0; x < width; x++)
            out[n * width + x] = acc.buf[src * width + x];
    }
    free(acc.buf);
    return n;
}
