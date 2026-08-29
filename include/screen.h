#ifndef WIN_TERMUX_SCREEN_H
#define WIN_TERMUX_SCREEN_H

#include "common.h"
#include "types.h"
#include "utf8.h"

int screen_init(ScreenBuffer *s, int cols, int rows);
void screen_free(ScreenBuffer *s);
int screen_resize(ScreenBuffer *s, int nc, int nr);
int screen_ensure_line(ScreenBuffer *s, int pr);

static inline int screen_phys_row(ScreenBuffer *s, int rel_row) {
    int r = (s->scroll_top + rel_row) % s->total_lines;
    if (r < 0) r += s->total_lines;
    return r;
}

static inline int screen_to_abs_row(ScreenBuffer *s, int cy, int vo) {
    if (s->in_alt_screen) return cy;
    return s->hist_lines - vo + cy;
}

CHAR_INFO *screen_cell(ScreenBuffer *s, int row, int col);
void screen_write_cell(ScreenBuffer *s, int row, int col, WCHAR ch, WORD attr);
void screen_scroll_up(ScreenBuffer *s, int top, int bottom, int count);
void screen_scroll_down(ScreenBuffer *s, int top, int bottom, int count);
void screen_newline(ScreenBuffer *s);
void detect_conpty_width(ScreenBuffer *s, int written_len);
WORD build_attr(ScreenBuffer *s);
void cell_truecolor(ScreenBuffer *s, int row, int col, int ar, WORD *out_f, WORD *out_b, int *out_fv, int *out_bv);

/* 选区边界吸附到完整字符（避免选中半个宽字符）。line 为一行的 WCHAR，
 * ncols 为宽度；返回夹紧/吸附后的列号。 */
int snap_right_to_char(const WCHAR *line, int ncols, int x);
int snap_left_to_char(const WCHAR *line, int ncols, int x);

#endif // WIN_TERMUX_SCREEN_H
