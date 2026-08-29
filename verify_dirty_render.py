#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""脏区渲染回归 (v1.8.12)。

从真源码 src/framediff.c 抽出整帧增量逻辑，用 gcc + ASAN/UBSan 编译执行。

framediff 把整帧 VT 流按绝对光标定位（CUP \x1b[r;cH）切成「每行一段字节」，
与上一帧影子逐行 memcmp：没变的行不发。验证：

1. 首帧整行都发（影子为空）。
2. 内容不变的第二帧：增量为空（0 字节）—— 这是省字节的关键。
3. 只有第 5 行变化：增量里只含第 5 行的 CUP 段，不含其它行。
4. always 段（OSC 标题 \x1b]0;..\x07）每帧都发。
5. invalidate / 终端行数变化后强制整帧。
6. 光标显隐序列（\x1b[?25h/l）随所在行走，光标移动行会脏。
"""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = (ROOT / "src" / "framediff.c").read_text(encoding="utf-8")

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "framediff.h"

static int failures = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } \
} while (0)

/* 构造一行终端内容：CUP 到 (row,col1) + SGR + 文本 + 复位。 */
static char *row_line(char *buf, size_t cap, int row, int col, const char *sgr, const char *text) {
    snprintf(buf, cap, "\x1b[%d;%dH%s%s\x1b[0m", row, col, sgr, text);
    return buf;
}

static char *frame_full(char *buf, size_t cap, int rows, const char *tag) {
    size_t pos = 0;
    for (int r = 1; r <= rows; r++) {
        char line[256];
        row_line(line, sizeof(line), r, 1, "\x1b[0;38;2;200;200;200m", tag);
        /* 每行不同文本，避免完全相同 */
        char seg[320];
        snprintf(seg, sizeof(seg), "\x1b[%d;1H%srow-%d-%s\x1b[0m",
                 r, "\x1b[0;38;2;200;200;200m", r, tag);
        size_t n = strlen(seg);
        if (pos + n < cap) { memcpy(buf + pos, seg, n); pos += n; }
    }
    buf[pos] = 0;
    return buf;
}

static void test_basic(void) {
    FrameDiff fd; framediff_init(&fd);
    char big[65536];

    /* 帧 1：3 行 + 一个 OSC 标题（always）。 */
    char f1[1024];
    size_t p = 0;
    const char *osc = "\x1b]0;termux\x07";
    memcpy(f1 + p, osc, strlen(osc)); p += strlen(osc);
    char ln[128];
    for (int r = 1; r <= 3; r++) {
        row_line(ln, sizeof(ln), r, 1, "\x1b[0;38;2;10;20;30m", "hello");
        size_t n = strlen(ln);
        memcpy(f1 + p, ln, n); p += n;
    }
    f1[p] = 0;

    char out[65536];
    framediff_begin_frame(&fd, 3);
    framediff_scan(&fd, f1, strlen(f1));
    size_t n1 = framediff_emit(&fd, NULL, 0); (void)n1;
    CHECK(n1 > 0, "first frame should emit something");
    { size_t _n = framediff_emit(&fd, out, sizeof(out)); out[_n] = 0; }
    CHECK(strstr(out, osc) != NULL, "OSC title must be in always segment");
    CHECK(strstr(out, "hello") != NULL, "first frame must contain row text");

    /* 帧 2：完全相同 -> 增量应只有 always（OSC），不含任何行文本。 */
    framediff_begin_frame(&fd, 3);
    framediff_scan(&fd, f1, strlen(f1));
    size_t n2 = framediff_emit(&fd, NULL, 0); (void)n2;
    { size_t _n = framediff_emit(&fd, out, sizeof(out)); out[_n] = 0; }
    CHECK(strstr(out, osc) != NULL, "OSC must still emit on unchanged frame");
    CHECK(strstr(out, "hello") == NULL,
          "unchanged rows must NOT be emitted, but found row text (n2=%zu)", n2);

    /* 帧 3：只改第 2 行。 */
    char f3[1024];
    p = 0;
    memcpy(f3 + p, osc, strlen(osc)); p += strlen(osc);
    for (int r = 1; r <= 3; r++) {
        const char *txt = (r == 2) ? "CHANGED" : "hello";
        row_line(ln, sizeof(ln), r, 1, "\x1b[0;38;2;10;20;30m", txt);
        size_t n = strlen(ln);
        memcpy(f3 + p, ln, n); p += n;
    }
    f3[p] = 0;
    framediff_begin_frame(&fd, 3);
    framediff_scan(&fd, f3, strlen(f3));
    size_t n3 = framediff_emit(&fd, NULL, 0); (void)n3;
    { size_t _n = framediff_emit(&fd, out, sizeof(out)); out[_n] = 0; }
    CHECK(strstr(out, "CHANGED") != NULL, "changed row 2 must emit");
    /* 第 1/3 行未变：它们的文本 "hello" 不应出现（第 2 行已不含 hello）。 */
    char *cup2 = strstr(out, "\x1b[2;1H");
    CHECK(cup2 != NULL, "changed row must carry its CUP \\x1b[2;1H");
    char *cup1 = strstr(out, "\x1b[1;1H");
    char *cup3 = strstr(out, "\x1b[3;1H");
    CHECK(cup1 == NULL && cup3 == NULL, "unchanged rows 1 and 3 must not emit");
    (void)big;

    framediff_free(&fd);
}

static void test_invalidate_and_resize(void) {
    FrameDiff fd; framediff_init(&fd);
    char f1[4096];
    frame_full(f1, sizeof(f1), 4, "A");

    char out[65536];
    framediff_begin_frame(&fd, 4);
    framediff_scan(&fd, f1, strlen(f1));
    framediff_emit(&fd, NULL, 0);
    { size_t _n = framediff_emit(&fd, out, sizeof(out)); out[_n] = 0; }

    /* 相同帧 -> 无行文本 */
    framediff_begin_frame(&fd, 4);
    framediff_scan(&fd, f1, strlen(f1));
    size_t n2 = framediff_emit(&fd, NULL, 0); (void)n2;
    { size_t _n = framediff_emit(&fd, out, sizeof(out)); out[_n] = 0; }
    CHECK(strstr(out, "row-1-A") == NULL, "unchanged frame should skip rows");

    /* invalidate 后整帧 */
    framediff_invalidate(&fd);
    framediff_begin_frame(&fd, 4);
    framediff_scan(&fd, f1, strlen(f1));
    framediff_emit(&fd, NULL, 0);
    { size_t _n = framediff_emit(&fd, out, sizeof(out)); out[_n] = 0; }
    CHECK(strstr(out, "row-1-A") != NULL, "after invalidate all rows must emit");

    /* 行数变化（resize）后整帧 */
    framediff_begin_frame(&fd, 4);
    framediff_scan(&fd, f1, strlen(f1));
    framediff_emit(&fd, NULL, 0);
    framediff_emit(&fd, out, sizeof(out));   /* 建立 4 行影子 */
    char f5[4096];
    frame_full(f5, sizeof(f5), 5, "A");
    framediff_begin_frame(&fd, 5);
    framediff_scan(&fd, f5, strlen(f5));
    framediff_emit(&fd, NULL, 0);
    { size_t _n = framediff_emit(&fd, out, sizeof(out)); out[_n] = 0; }
    CHECK(strstr(out, "row-5-A") != NULL, "after resize new rows must emit");
    CHECK(strstr(out, "row-1-A") != NULL, "after resize existing rows must also emit (force all)");

    framediff_free(&fd);
}

static void test_cursor_toggle(void) {
    FrameDiff fd; framediff_init(&fd);
    char out[65536];

    /* 帧：行 1-3，光标在第 2 行末 \x1b[?25h */
    char f1[1024];
    snprintf(f1, sizeof(f1),
             "\x1b[1;1H%sone\x1b[0m"
             "\x1b[2;1H%stwo\x1b[0m"
             "\x1b[3;1H%sthree\x1b[0m"
             "\x1b[2;5H\x1b[?25h",
             "\x1b[0;38;2;1;2;3m", "\x1b[0;38;2;1;2;3m", "\x1b[0;38;2;1;2;3m");
    framediff_begin_frame(&fd, 3);
    framediff_scan(&fd, f1, strlen(f1));
    framediff_emit(&fd, NULL, 0);
    { size_t _n = framediff_emit(&fd, out, sizeof(out)); out[_n] = 0; }

    /* 帧 2：内容相同，光标移到第 3 行 -> 第 2、3 行都脏（2 去掉光标，3 加上）。 */
    char f2[1024];
    snprintf(f2, sizeof(f2),
             "\x1b[1;1H%sone\x1b[0m"
             "\x1b[2;1H%stwo\x1b[0m"
             "\x1b[3;1H%sthree\x1b[0m"
             "\x1b[3;6H\x1b[?25h",
             "\x1b[0;38;2;1;2;3m", "\x1b[0;38;2;1;2;3m", "\x1b[0;38;2;1;2;3m");
    framediff_begin_frame(&fd, 3);
    framediff_scan(&fd, f2, strlen(f2));
    framediff_emit(&fd, NULL, 0);
    { size_t _n = framediff_emit(&fd, out, sizeof(out)); out[_n] = 0; }
    CHECK(strstr(out, "\x1b[3;6H") != NULL, "cursor move to row 3 must dirty row 3");
    CHECK(strstr(out, "\x1b[2;1H") != NULL, "old cursor row 2 must re-emit (cursor left it)");
    CHECK(strstr(out, "\x1b[1;1H") == NULL, "unrelated row 1 must not emit");

    framediff_free(&fd);
}

int main(void) {
    test_basic();
    test_invalidate_and_resize();
    test_cursor_toggle();
    if (failures) { printf("[FAIL] %d check(s) failed\n", failures); return 1; }
    printf("[OK] 脏区渲染验证通过（首帧全发 / 未变帧 0 行 / 只发变化行 / OSC always / invalidate / resize / 光标随行）。\n");
    return 0;
}
"""


def main():
    print("=== 脏区渲染 (verify_dirty_render.py) ===")
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        (td / "harness.c").write_text(HARNESS, encoding="utf-8")
        exe = td / "t"
        cmd = ["gcc", "-O1", "-g", "-fsanitize=address,undefined",
               "-Wall", "-Wextra", "-Werror",
               "-I", str(ROOT / "include"),
               str(ROOT / "src" / "framediff.c"),
               str(td / "harness.c"), "-o", str(exe)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stdout)
            print(r.stderr)
            sys.exit("FAIL: compile error")
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        print(r.stdout)
        if r.returncode != 0:
            print(r.stderr)
            sys.exit(1)


if __name__ == "__main__":
    main()
