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
            "void cell_truecolor("):
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
} ScreenBuffer;

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

int main(void) {
    if (test_alt_resize_truecolor()) return 1;
    if (test_search_cur_after_drop()) return 1;
    printf("  [OK] screen.c 状态迁移验证通过（alt 屏真彩色迁移 + 搜索当前项落点）。\n");
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
