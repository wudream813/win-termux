#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""screen.c 状态迁移回归 (v1.8.11)：alt 屏 resize 的真彩色标记 + 搜索当前项落点。

两条不变量，都是从 src/screen.c 抽真源码编译执行来验证的（改坏 C 代码这里会红）：

BUG-8  alt 屏（vim / htop 这类全屏程序）改窗口大小时，一行里的
       cells / fg_rgb / bg_rgb / rgb_valid 四个并行数组必须都搬 cc 个元素。
       历史上 rgb_valid 只搬了每行第 0 列，于是除最左一列外整屏真彩色被清空，
       颜色退化成 16 色。

BUG-9  搜索结果被新输出挤出滚动缓冲时，如果用户正停留的那一条被剔除，
       光标应当落到「它之后最近的存活项」（剔除总是从最老一端发生，所以就是
       index 0），而不是弹到最新的一条。
"""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
hdr = (ROOT / "include" / "screen.h").read_text(encoding="utf-8")
src = (ROOT / "src" / "screen.c").read_text(encoding="utf-8")


def extract_func(text, prefix):
    idx = text.find(prefix)
    if idx == -1:
        sys.exit("FAIL: %s not found" % prefix)
    brace = text.find("{", idx)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[idx:i + 1]
    sys.exit("FAIL: unbalanced braces for %s" % prefix)


PIECES = [extract_func(hdr, "static inline int screen_phys_row(")]
for sig in ("static void line_free(",
            "static void line_fill_blank(",
            "static int line_alloc(",
            "static void line_copy(",
            "static void alt_row_copy(",
            "int screen_ensure_line(",
            "void screen_write_cell(",
            "void screen_scroll_up(",
            "int screen_resize(",
            "void cell_truecolor(",
            "void screen_mark_softwrap(",
            "static int reflow_glyph_w(",
            "static int reflow_append_rows(",
            "static int reflow_acc_cb(",
            "int screen_reflow_view("):
    PIECES.append(extract_func(src, sig))

PRELUDE = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define MAX_PANES 16
#define MAX_SEARCH_MATCHES 2048
#define RGB565_WHITE 0xFFFF
#define RGB565_BLACK 0x0000

typedef unsigned short WORD;
typedef unsigned short WCHAR;

static inline WORD rgb565(int r, int g, int b) {
    return (WORD)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

typedef struct {
    union { WCHAR UnicodeChar; char AsciiChar; } Char;
    WORD Attributes;
} CHAR_INFO;

typedef struct {
    CHAR_INFO *cells;
    WORD *fg_rgb;
    WORD *bg_rgb;
    unsigned char *rgb_valid;
    int len;
} ScreenLine;

typedef struct {
    ScreenLine *lines;
    int cols, rows, total_lines, scroll_top;
    int cursor_x, cursor_y, cursor_visible;
    WORD current_attr;
    int fg_color, bg_color, bold, underline, reverse_video;
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
    unsigned char *line_wrap;
} ScreenBuffer;

typedef struct {
    CHAR_INFO ci;
    WORD fg, bg;
    unsigned char v;
} RGlyph;

typedef struct {
    RGlyph *buf;
    int cap, count, width;
} RfAcc;

/* reflow 片段里宽字符判定：stub 只给 CJK/全角常见区间，足够测试（ASCII 窄）。 */
static int is_wide_cp(unsigned int cp) {
    (void)cp; return 0;
}

typedef struct {
    int active;
    void *hpc;
    void *pipe_in, *pipe_out, *process, *thread, *read_thread;
    ScreenBuffer screen;
    char title[64];
    char full_title[256];
    int scroll_offset;
    int color;
    int is_settings, is_about, exited_hold;
    unsigned long exit_code;
    WCHAR input_history[256];
    int input_history_len, input_history_pos;
} Pane;

typedef struct { int abs_y; int start_x; int end_x; } SearchMatch;
typedef struct { Pane panes[MAX_PANES]; int pane_count, active_pane; } MuxState;

MuxState g_mux;
SearchMatch g_search_matches[MAX_SEARCH_MATCHES];
int g_search_match_count = 0;
int g_search_match_cur = -1;
int g_search_active = 0;
int g_scrollback_lines = 200;
"""

DRIVER = r"""
static void alloc_alt(ScreenBuffer *s, int cols, int rows) {
    s->cols = cols; s->rows = rows;
    s->total_lines = rows + g_scrollback_lines;
    s->current_attr = 0x07;
    s->lines = (ScreenLine *)calloc(s->total_lines, sizeof(ScreenLine));
    /* 可见行预分配 cols 宽（真实程序 screen_init 如此），len=cols；历史行延迟分配。 */
    for (int i = 0; i < s->total_lines; i++)
        assert(line_alloc(&s->lines[i], cols, 0x07));
    s->alt_buffer = (CHAR_INFO *)calloc(rows * cols, sizeof(CHAR_INFO));
    s->alt_fg_rgb = (WORD *)calloc(rows * cols, sizeof(WORD));
    s->alt_bg_rgb = (WORD *)calloc(rows * cols, sizeof(WORD));
    s->alt_rgb_valid = (unsigned char *)calloc(rows * cols, 1);
    assert(s->lines && s->alt_buffer && s->alt_fg_rgb && s->alt_bg_rgb && s->alt_rgb_valid);
}

static void free_screen(ScreenBuffer *s) {
    if (s->lines) {
        for (int i = 0; i < s->total_lines; i++) line_free(&s->lines[i]);
        free(s->lines);
        s->lines = NULL;
    }
    free(s->line_wrap); s->line_wrap = NULL;
    free(s->alt_buffer); free(s->alt_fg_rgb); free(s->alt_bg_rgb); free(s->alt_rgb_valid);
    s->alt_buffer = NULL; s->alt_fg_rgb = NULL; s->alt_bg_rgb = NULL; s->alt_rgb_valid = NULL;
}

/* BUG-8: alt 屏 resize 后每一列的真彩色标记都必须还在。 */
static int test_alt_resize_truecolor(void) {
    ScreenBuffer s;
    memset(&s, 0, sizeof(s));
    alloc_alt(&s, 16, 6);
    s.in_alt_screen = 1;
    s.fg_rgb_on = 1; s.bg_rgb_on = 1;
    s.fg_r = 200; s.fg_g = 100; s.fg_b = 50;
    s.bg_r = 10;  s.bg_g = 20;  s.bg_b = 30;
    for (int y = 0; y < s.rows; y++)
        for (int x = 0; x < s.cols; x++)
            screen_write_cell(&s, y, x, L'X', 0x07);

    WORD want_f = rgb565(200, 100, 50), want_b = rgb565(10, 20, 30);
    assert(screen_resize(&s, 24, 6) == 1);

    int bad = 0;
    for (int y = 0; y < 6; y++) {
        for (int x = 0; x < 16; x++) {          /* 迁移过来的那 16 列 */
            WORD f, b; int fv, bv;
            cell_truecolor(&s, y, x, -1, &f, &b, &fv, &bv);
            if (!fv || !bv || f != want_f || b != want_b) {
                if (!bad) fprintf(stderr, "FAIL: resize 后 (%d,%d) 丢了真彩色 fv=%d bv=%d\n", y, x, fv, bv);
                bad++;
            }
        }
    }
    if (bad) { free_screen(&s); fprintf(stderr, "FAIL: %d 个 cell 的真彩色标记在 resize 中丢失\n", bad); return 1; }
    free_screen(&s);
    printf("  BUG-8: alt 屏 16x6 -> 24x6，96 个 cell 的真彩色标记全部保留\n");
    return 0;
}

/* BUG-9: 当前停留的匹配被剔除后，落到存活项里最近的那个（index 0），不是最新的。 */
static int test_search_cur_after_drop(void) {
    ScreenBuffer s;
    memset(&s, 0, sizeof(s));
    memset(&g_mux, 0, sizeof(g_mux));
    g_scrollback_lines = 8;
    alloc_alt(&s, 10, 4);
    s.in_alt_screen = 0;
    s.pane_index = 0;
    g_mux.active_pane = 0;
    g_mux.pane_count = 1;
    g_mux.panes[0].active = 1;

    /* 填满历史，逼近容量上限，之后每滚一行就会丢掉最老的一行。 */
    int cap = s.total_lines - s.rows;
    for (int i = 0; i < cap; i++) screen_scroll_up(&s, 0, s.rows - 1, 1);
    assert(s.hist_lines == cap);

    g_search_active = 1;
    g_search_match_count = 3;
    g_search_matches[0].abs_y = 0;   /* 最老 */
    g_search_matches[1].abs_y = 3;
    g_search_matches[2].abs_y = 6;   /* 最新 */
    g_search_match_cur = 0;          /* 用户正停在最老的那一条 */

    screen_scroll_up(&s, 0, s.rows - 1, 2);   /* 挤掉最老的 2 行 -> 剔除 match 0 */

    if (g_search_match_count != 2) {
        fprintf(stderr, "FAIL: 剔除后应剩 2 条，实际 %d\n", g_search_match_count);
        free_screen(&s); return 1;
    }
    if (g_search_match_cur != 0) {
        fprintf(stderr, "FAIL: 当前项应落到存活项里最近的一条 (index 0)，实际 %d\n", g_search_match_cur);
        free_screen(&s); return 1;
    }
    if (g_search_matches[g_search_match_cur].abs_y != 1) {
        fprintf(stderr, "FAIL: 落点 abs_y 应为 1（原 abs_y=3 平移 2），实际 %d\n",
                g_search_matches[g_search_match_cur].abs_y);
        free_screen(&s); return 1;
    }

    /* 当前项没被剔除时，必须跟着平移、指向同一条。 */
    g_search_match_cur = 1;                   /* 现在指向 abs_y = 4（原 6） */
    int keep_abs = g_search_matches[1].abs_y;
    screen_scroll_up(&s, 0, s.rows - 1, 1);
    if (g_search_match_cur != 1 || g_search_matches[1].abs_y != keep_abs - 1) {
        fprintf(stderr, "FAIL: 未被剔除的当前项没有正确跟随平移\n");
        free_screen(&s); return 1;
    }
    free_screen(&s);
    printf("  BUG-9: 当前项被剔除 -> 落到最近的存活项；未被剔除 -> 原样跟随平移\n");
    return 0;
}

/* v1.8.42: 分屏拖条 resize 高度缩小时，旧可见区顶部行必须滚入历史、不能丢失。 */
static char at_rel(ScreenBuffer *s, int rel) {
    int pr = screen_phys_row(s, rel);
    if (pr < 0 || !s->lines[pr].cells) return '?';
    return (char)s->lines[pr].cells[0].Char.UnicodeChar;
}
/* v1.8.43: 宽窗格跑出历史后【收窄宽度】，历史必须逐行完整保留、内容不错位。 */
static int test_resize_narrow_keeps_history(void) {
    ScreenBuffer s;
    memset(&s, 0, sizeof(s));
    g_scrollback_lines = 1000;
    alloc_alt(&s, 80, 10);
    s.in_alt_screen = 0;
    /* 写 10 行可见内容 A..J（可见行 0..9 各一行）。 */
    for (int y = 0; y < 10; y++)
        screen_write_cell(&s, y, 0, (WCHAR)('A' + y), 0x07);
    /* 再滚 10 行、每行在新底行写 K..T，制造 10 行历史（A..J 进历史）。 */
    for (int i = 0; i < 10; i++) {
        screen_scroll_up(&s, 0, s.rows - 1, 1);
        screen_write_cell(&s, s.rows - 1, 0, (WCHAR)('K' + i), 0x07);
    }
    if (s.hist_lines != 10) {
        fprintf(stderr, "FAIL: 收窄前 hist 应为 10，实际 %d\n", s.hist_lines);
        free_screen(&s); return 1;
    }
    /* 收窄宽度 80 -> 20（高度不变）。 */
    assert(screen_resize(&s, 20, 10) == 1);
    if (s.hist_lines != 10) {
        fprintf(stderr, "FAIL: 收窄宽度后 hist 应仍为 10，实际 %d\n", s.hist_lines);
        free_screen(&s); return 1;
    }
    /* 历史行内容（首列标记）必须与收窄前一致：可见 0..9 = K..T，历史 -1=J .. -10=A。 */
    for (int y = 0; y < 10; y++) {
        char want = (char)('K' + y);   /* 可见行 y 写的是 K+y（K..T） */
        if (at_rel(&s, y) != want) {
            fprintf(stderr, "FAIL: 收窄后可见行 %d 应为 %c，实际 %c\n", y, want, at_rel(&s, y));
            free_screen(&s); return 1;
        }
    }
    for (int h = 1; h <= 10; h++) {
        char want = (char)('J' - h + 1);   /* 历史 -1=J,-2=I,...,-10=A */
        if (at_rel(&s, -h) != want) {
            fprintf(stderr, "FAIL: 收窄后历史 -%d 应为 %c，实际 %c（历史错位/丢失）\n",
                    h, want, at_rel(&s, -h));
            free_screen(&s); return 1;
        }
    }
    free_screen(&s);
    printf("  v1.8.43: 宽窗格 80 列收窄到 20 列，10 行历史逐行完整保留\n");
    return 0;
}

/* v1.8.45 #6：左右分屏调大小（宽度收窄）时，历史里的【宽行】右侧不能被硬截断。
 * 写一满行（80 列，列 0..79 各放 'A'+x%26），滚 1 行进历史，再收窄 80->20；
 * 收窄后该行作为历史 -1，其第 21..79 列内容必须仍在（拖宽回来可见）。 */
static int test_resize_narrow_wide_history_not_truncated(void) {
    ScreenBuffer s;
    memset(&s, 0, sizeof(s));
    g_scrollback_lines = 1000;
    alloc_alt(&s, 80, 10);
    s.in_alt_screen = 0;
    /* 满行写在可见行 0（scroll 1 次后整行滚出可见区、成为历史 -1；
     * 写在行 9 会因 scroll 向上平移而留在可见区）。 */
    for (int x = 0; x < 80; x++)
        screen_write_cell(&s, 0, x, (WCHAR)('A' + (x % 26)), 0x07);
    screen_scroll_up(&s, 0, s.rows - 1, 1);   /* 满行滚入历史 -1 */
    if (s.hist_lines != 1) { fprintf(stderr, "FAIL: 准备 hist 应为 1，实际 %d\n", s.hist_lines); free_screen(&s); return 1; }
    assert(screen_resize(&s, 20, 10) == 1);   /* 收窄 80->20 */
    if (s.hist_lines != 1) { fprintf(stderr, "FAIL: 收窄后 hist 应仍 1，实际 %d\n", s.hist_lines); free_screen(&s); return 1; }
    int pr = screen_phys_row(&s, -1);
    if (!s.lines[pr].cells || s.lines[pr].len < 80) {
        fprintf(stderr, "FAIL: 收窄后历史宽行被截断: len=%d（应保留 80）\n", s.lines[pr].len);
        free_screen(&s); return 1;
    }
    for (int x = 0; x < 80; x++) {
        WCHAR want = (WCHAR)('A' + (x % 26));
        WCHAR got = s.lines[pr].cells[x].Char.UnicodeChar;
        if (got != want) {
            fprintf(stderr, "FAIL: 收窄后历史宽行第 %d 列被截断/改动: 得 %c want %c\n", x, (char)got, (char)want);
            free_screen(&s); return 1;
        }
    }
    /* 再拖宽回 60：历史行仍应保留完整 80 列（含 60..79）。 */
    assert(screen_resize(&s, 60, 10) == 1);
    pr = screen_phys_row(&s, -1);
    if (!s.lines[pr].cells || s.lines[pr].len < 80) {
        fprintf(stderr, "FAIL: 再拖宽后历史宽行 len=%d（应保留 80）\n", s.lines[pr].len);
        free_screen(&s); return 1;
    }
    for (int x = 0; x < 80; x++) {
        WCHAR want = (WCHAR)('A' + (x % 26));
        WCHAR got = s.lines[pr].cells[x].Char.UnicodeChar;
        if (got != want) {
            fprintf(stderr, "FAIL: 再拖宽后历史宽行第 %d 列丢失: 得 %c want %c\n", x, (char)got, (char)want);
            free_screen(&s); return 1;
        }
    }
    free_screen(&s);
    printf("  v1.8.45: 收窄 80->20->60，历史宽行 80 列完整保留不截断\n");
    return 0;
}

/* v1.8.44：宽窗格跑出长历史后，同时【收窄宽度 + 缩小高度】（模拟把分屏条拖小），
 * 底部命令行必须留在新可见底部、老行滚入历史且顺序不错乱。 */
static int test_resize_wide_then_small(void) {
    ScreenBuffer s;
    memset(&s, 0, sizeof(s));
    g_scrollback_lines = 1000;
    alloc_alt(&s, 80, 20);
    s.in_alt_screen = 0;
    /* 先写 20 行可见（行 0..19），再滚 15 行、底行续写，制造 15 行历史。
     * 旧可见行 y（0..19）对应全局行号 15+y：标记 '0'+((15+y)%10)；
     * 旧历史 -h 对应全局行号 15-h。 */
    for (int y = 0; y < 20; y++)
        screen_write_cell(&s, y, 0, (WCHAR)('0' + (y % 10)), 0x07);
    for (int i = 0; i < 15; i++) {
        screen_scroll_up(&s, 0, s.rows - 1, 1);
        screen_write_cell(&s, s.rows - 1, 0, (WCHAR)('0' + ((20 + i) % 10)), 0x07);
    }
    /* 同时收窄 80->30、缩小 20->8。resize 不改历史（hist 仍 15），可见区底部
     * 锚定：新可见 y = 旧可见 (12+y)。被裁的顶部 12 行【不】滚入历史（否则与
     * ConPTY reflow 重发重复——v1.8.45 修复）。 */
    assert(screen_resize(&s, 30, 8) == 1);
    if (s.rows != 8 || s.cols != 30) { fprintf(stderr, "FAIL: resize 后尺寸 %dx%d\n", s.cols, s.rows); free_screen(&s); return 1; }
    if (s.hist_lines != 15) { fprintf(stderr, "FAIL: resize 不应改历史，hist 应仍为 15，实际 %d\n", s.hist_lines); free_screen(&s); return 1; }
    /* 新可见 y（0..7）底部锚定 = 旧可见 (12+y)，标记 '0'+((15+12+y)%10)。 */
    for (int y = 0; y < 8; y++) {
        char want = (char)('0' + ((15 + 12 + y) % 10));
        if (at_rel(&s, y) != want) {
            fprintf(stderr, "FAIL: 底部锚定新可见 y=%d 应保留旧可见(12+y)，得 %c want %c\n",
                    y, at_rel(&s, y), want);
            free_screen(&s); return 1;
        }
    }
    /* 历史不变：新历史 -h = 旧历史 -h（全局行号 15-h）。 */
    for (int h = 1; h <= 15; h++) {
        char want = (char)('0' + ((15 - h) % 10));
        if (at_rel(&s, -h) != want) {
            fprintf(stderr, "FAIL: resize 后历史 -%d 被改动: 得 %c want %c\n", h, at_rel(&s,-h), want);
            free_screen(&s); return 1;
        }
    }
    free_screen(&s);
    printf("  v1.8.45: 80x20 收窄到 30x8 不改历史、可见区底部锚定（修历史重复）\n");
    return 0;
}

static int test_resize_shrink_keeps_history(void) {
    ScreenBuffer s;
    memset(&s, 0, sizeof(s));
    g_scrollback_lines = 1000;
    alloc_alt(&s, 20, 10);
    s.in_alt_screen = 0;
    for (int y = 0; y < 10; y++) screen_write_cell(&s, y, 0, (WCHAR)('A' + y), 0x07);
    /* 滚 3 行：A,B,C 进历史；可见底部再写 x,y,z。 */
    screen_scroll_up(&s, 0, s.rows - 1, 3);
    screen_write_cell(&s, 7, 0, L'x', 0x07);
    screen_write_cell(&s, 8, 0, L'y', 0x07);
    screen_write_cell(&s, 9, 0, L'z', 0x07);
    if (s.hist_lines != 3) { fprintf(stderr, "FAIL: 缩小前 hist 应为 3，实际 %d\n", s.hist_lines); return 1; }

    /* 高度 10 -> 6（底部锚定，历史不随 resize 变动）：
     *   新可见 = 旧可见底部 6 行 H,I,J,x,y,z（0=H..5=z，命令行 z 留底）；
     *   被裁的顶部 D,E,F,G 不滚入历史（否则与 ConPTY reflow 重发重复），
     *   hist 仍为 3（-1=C,-2=B,-3=A）。 */
    assert(screen_resize(&s, 20, 6) == 1);
    if (s.rows != 6) { fprintf(stderr, "FAIL: resize 后 rows!=6\n"); return 1; }
    if (s.hist_lines != 3) { fprintf(stderr, "FAIL: resize 不应改历史，hist 应仍为 3，实际 %d\n", s.hist_lines); free_screen(&s); return 1; }
    if (at_rel(&s,0)!='H' || at_rel(&s,5)!='z') {
        fprintf(stderr, "FAIL: 底部锚定下新可见应保留底部 H..z: 0=%c 5=%c\n", at_rel(&s,0), at_rel(&s,5));
        free_screen(&s); return 1;
    }
    if (at_rel(&s,-1)!='C' || at_rel(&s,-2)!='B' || at_rel(&s,-3)!='A') {
        fprintf(stderr, "FAIL: 旧历史 A..C 被改动: -1=%c -2=%c -3=%c\n",
                at_rel(&s,-1), at_rel(&s,-2), at_rel(&s,-3));
        free_screen(&s); return 1;
    }
    /* 放大回 10 行（底部锚定）：z 仍在新可见底部 y9，H 在 y4；顶部空出的行从
     * 历史回补（ConPTY 随后会重绘可见区）。hist 仍为 3。 */
    assert(screen_resize(&s, 20, 10) == 1);
    if (s.rows != 10) { fprintf(stderr, "FAIL: 放大后 rows!=10\n"); free_screen(&s); return 1; }
    if (s.hist_lines != 3) { fprintf(stderr, "FAIL: 放大后 hist 应为 3，实际 %d\n", s.hist_lines); free_screen(&s); return 1; }
    if (at_rel(&s,4)!='H' || at_rel(&s,9)!='z') {
        fprintf(stderr, "FAIL: 放大后底部锚定 4=H 9=z: 4=%c 9=%c\n", at_rel(&s,4), at_rel(&s,9));
        free_screen(&s); return 1;
    }
    if (at_rel(&s,-1)!='C' || at_rel(&s,-3)!='A') {
        fprintf(stderr, "FAIL: 放大后旧历史 A..C 丢失: -1=%c -3=%c\n", at_rel(&s,-1), at_rel(&s,-3));
        free_screen(&s); return 1;
    }
    /* 加宽不改行数、不丢历史（此时历史为 A,B,C，可见底部为 z）。 */
    int hb = s.hist_lines;
    assert(screen_resize(&s, 40, 10) == 1);
    if (s.hist_lines != hb || at_rel(&s,-1)!='C' || at_rel(&s,-3)!='A' || at_rel(&s,9)!='z') {
        fprintf(stderr, "FAIL: 加宽后历史/内容丢失: hist=%d -3=%c -1=%c 9=%c\n",
                s.hist_lines, at_rel(&s,-3), at_rel(&s,-1), at_rel(&s,9));
        free_screen(&s); return 1;
    }
    free_screen(&s);
    printf("  v1.8.45: resize 高度 10->6->10 不改历史、可见区底部锚定（命令行留底）\n");
    return 0;
}

/* v1.8.47：reflow 历史视图——一条跨越软换行的长逻辑行，在窄视口下应折成多行
 * 完整显示（内容不丢、不重），宽视口折回一行。 */
static int test_reflow_view(void) {
    ScreenBuffer s;
    memset(&s, 0, sizeof(s));
    g_scrollback_lines = 1000;
    alloc_alt(&s, 10, 6);
    s.in_alt_screen = 0;
    s.line_wrap = (unsigned char *)calloc(s.total_lines, 1);
    /* 宽 10 高 6。逻辑行 "abcdefghij"（10 字符，硬换行 wrap=0）写在可见行 0，
     * 滚动 1 行后成为历史 -1；可见区变空白。 */
    for (int x = 0; x < 10; x++) screen_write_cell(&s, 0, x, (WCHAR)('a'+x), 0x07);
    screen_scroll_up(&s, 0, s.rows - 1, 1);
    if (s.hist_lines < 1) { fprintf(stderr, "FAIL: reflow 准备 hist<1\n"); return 1; }

    /* vo=2：跳过最新 2 显示行（ij、efgh），视口底部=abcd。 */
    RGlyph *out = (RGlyph *)calloc(6 * 4, sizeof(RGlyph));
    int n = screen_reflow_view(&s, 2, 6, 4, out);
    if (n != 1) { fprintf(stderr, "FAIL: reflow_view 返回 %d（应1）\n", n); free(out); free_screen(&s); return 1; }
    for (int x = 0; x < 4; x++) {
        WCHAR got = out[5*4+x].ci.Char.UnicodeChar;
        char gc = (got==0||got==L' ')?' ':(char)got;
        char want = "abcd"[x];
        if (gc != want) {
            fprintf(stderr, "FAIL: vo=2 视口底部应=abcd 列%d 得 '%c' want '%c'\n", x, gc, want);
            free(out); free_screen(&s); return 1;
        }
    }
    free(out);

    /* vo=0：视口 y5=ij（命令行）、y4=efgh、y3=abcd，y2 空白。 */
    RGlyph *out2 = (RGlyph *)calloc(6 * 4, sizeof(RGlyph));
    screen_reflow_view(&s, 0, 6, 4, out2);
    {
        const char *r3 = "abcd", *r4 = "efgh", *r5 = "ij";
        const char *rp[6] = {"","","",r3,r4,r5};
        for (int y = 3; y < 6; y++) {
            for (int x = 0; x < 4; x++) {
                char want = x < (int)strlen(rp[y]) ? rp[y][x] : ' ';
                WCHAR got = out2[y*4+x].ci.Char.UnicodeChar;
                char gc = (got==0||got==L' ')?' ':(char)got;
                if (gc != want) {
                    fprintf(stderr, "FAIL: vo=0 y%d x%d: 得 '%c' want '%c'\n", y, x, gc, want);
                    free(out2); free_screen(&s); return 1;
                }
            }
        }
        WCHAR g2 = out2[2*4].ci.Char.UnicodeChar;
        if (g2 != L' ' && g2 != 0) { fprintf(stderr, "FAIL: vo=0 y2 应空白，得 '%c'\n", (char)g2); free(out2); free_screen(&s); return 1; }
    }
    free(out2);

    /* 宽视口 20：折 1 行 abcdefghij，vo=0 视口底部 y5=整行。 */
    RGlyph *out3 = (RGlyph *)calloc(6 * 20, sizeof(RGlyph));
    screen_reflow_view(&s, 0, 6, 20, out3);
    for (int x = 0; x < 10; x++) {
        WCHAR got = out3[5*20+x].ci.Char.UnicodeChar;
        if ((char)got != (char)('a'+x)) {
            fprintf(stderr, "FAIL: 宽视口 vo=0 底部 y5 列%d: 得 '%c' want '%c'\n", x, (char)got, 'a'+x);
            free(out3); free_screen(&s); return 1;
        }
    }
    free(out3);

    /* 软换行合并：历史 -1="xyz"(wrap=1) + -2="ab"(wrap=0) => 逻辑行 "abxyz"，
     * vo=0 视口底部 y3=abxyz（宽10折1行）。 */
    {
        ScreenBuffer t; memset(&t, 0, sizeof(t));
        alloc_alt(&t, 6, 4);
        t.in_alt_screen = 0;
        t.line_wrap = (unsigned char *)calloc(t.total_lines, 1);
        screen_scroll_up(&t, 0, t.rows-1, 2);
        int r_new = screen_phys_row(&t, -1);
        int r_old = screen_phys_row(&t, -2);
        for (int x=0;x<3;x++) t.lines[r_new].cells[x].Char.UnicodeChar = (WCHAR)('x'+x);
        for (int x=0;x<2;x++) t.lines[r_old].cells[x].Char.UnicodeChar = (WCHAR)('a'+x);
        t.line_wrap[r_new] = 1;
        t.line_wrap[r_old] = 0;
        RGlyph *o = (RGlyph *)calloc(4*10, sizeof(RGlyph));
        int nn = screen_reflow_view(&t, 0, 4, 10, o);
        if (nn != 1) { fprintf(stderr, "FAIL: 软换行 reflow 返回 %d\n", nn); free(o); free_screen(&t); free_screen(&s); return 1; }
        const char *want = "abxyz";
        for (int x=0;x<5;x++) {
            WCHAR got = o[3*10+x].ci.Char.UnicodeChar;
            if ((char)got != want[x]) {
                fprintf(stderr, "FAIL: 软换行合并 底部列%d: 得 '%c' want '%c'\n", x, (char)got, want[x]);
                free(o); free_screen(&t); free_screen(&s); return 1;
            }
        }
        free(o); free_screen(&t);
    }

    free_screen(&s);
    printf("  v1.8.47: reflow 统一视图（历史+可见）——窄折多行/宽折回/软换行合并/vo窗口底部锚定\n");
    return 0;
}

int main(void) {
    if (test_alt_resize_truecolor()) return 1;
    if (test_search_cur_after_drop()) return 1;
    if (test_resize_narrow_keeps_history()) return 1;
    if (test_resize_narrow_wide_history_not_truncated()) return 1;
    if (test_resize_wide_then_small()) return 1;
    if (test_resize_shrink_keeps_history()) return 1;
    if (test_reflow_view()) return 1;
    printf("  [OK] screen.c 状态迁移验证通过（alt 屏真彩色迁移 + 搜索当前项落点 + resize 保历史）。\n");
    return 0;
}
"""

CODE = PRELUDE + "\n" + "\n".join(PIECES) + "\n" + DRIVER


def main() -> int:
    print("=== screen.c 状态迁移回归 (verify_screen_state.py) ===")
    with tempfile.TemporaryDirectory() as td:
        c = Path(td) / "t.c"
        exe = Path(td) / "t.bin"
        c.write_text(CODE, encoding="utf-8")
        build = subprocess.run(
            ["gcc", "-O1", "-g", "-fsanitize=address,undefined", "-Wall", "-Wextra",
             "-o", str(exe), str(c)],
            capture_output=True, text=True)
        if build.returncode:
            print(build.stderr or build.stdout, file=sys.stderr)
            print("FAIL: 无法编译 screen.c 抽取出来的状态迁移代码", file=sys.stderr)
            return 1
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        print(run.stdout, end="")
        if run.returncode:
            print(run.stderr, file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
