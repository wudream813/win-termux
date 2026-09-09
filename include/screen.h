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
void screen_mark_softwrap(ScreenBuffer *s);  /* 标记当前行=软换行续行（v1.8.47 reflow） */
int screen_reflow_height(ScreenBuffer *s, int width); /* reflow 内容显示行总数（v1.8.52） */
int screen_scroll_limit(ScreenBuffer *s);             /* 回看 vo 上限（内容末贴视口顶），0=无 */

/* reflow 视图里的一个显示单元（字符 + 16 色属性 + 真彩）。 */
typedef struct {
    CHAR_INFO ci;
    WORD fg, bg;
    unsigned char v;
} RGlyph;

/* 生成滚动历史的 reflow 视图：把物理行按软换行标志合并成逻辑行、按 width 重新
 * 折行，返回向上回看 vo 个显示行时、视口 rows×width 的内容到 out（行主序）。
 * 返回有效行数；alt 屏/无 wrap 标志时返回 0（调用方回退到物理行直取）。 */
int screen_reflow_view(ScreenBuffer *s, int vo, int rows, int width, RGlyph *out);
/* 本地 reflow resize 后 ConPTY 会整屏重绘（重绘顶行按其自身滚动缓冲对齐）。若该块是
 * 重绘且其首行内容落在本地环更深处（可见区 rel>0），把环滚动对齐使 ConPTY 视口与本地
 * 一致（内容不变、历史行数增长），避免重绘覆盖吞行。命中并调整返回 1，否则 0。 */
int screen_repaint_align(ScreenBuffer *s, const char *data, int len);
void detect_conpty_width(ScreenBuffer *s, int written_len);
WORD build_attr(ScreenBuffer *s);
void cell_truecolor(ScreenBuffer *s, int row, int col, int ar, WORD *out_f, WORD *out_b, int *out_fv, int *out_bv);

/* 选区边界吸附到完整字符（避免选中半个宽字符）。line 为一行的 CHAR_INFO 单元格
 * （真实缓冲步长，勿传 WCHAR*），ncols 为宽度；返回夹紧/吸附后的列号。 */
int snap_right_to_char(const CHAR_INFO *line, int ncols, int x);
int snap_left_to_char(const CHAR_INFO *line, int ncols, int x);
/* 复制模式按字符移动：一次跨过整个宽字符，dir=+1 右 / -1 左。 */
int copy_step_char(const CHAR_INFO *line, int ncols, int x, int dir);
/* 复制模式光标列整字化：落在宽字符次格（半个字）则退到其主格。 */
int copy_cursor_to_lead(const CHAR_INFO *line, int ncols, int x);

#endif // WIN_TERMUX_SCREEN_H
