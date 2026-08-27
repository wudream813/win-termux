#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_ringbuf_asan.py - Real AddressSanitizer verification for win-termux ring buffer & scrolling.
Compiles ring buffer functions with gcc -fsanitize=address,undefined and tests 1200+ edge cases.
"""

import subprocess
import tempfile
import os
import sys

C_TEST_CODE = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define SCROLL_BUF_LINES 10000
#define RGB565_WHITE 0xFFFF
#define RGB565_BLACK 0x0000

typedef unsigned short WORD;
typedef unsigned short WCHAR;

typedef struct {
    union {
        WCHAR UnicodeChar;
        char   AsciiChar;
    } Char;
    WORD Attributes;
} CHAR_INFO;

typedef struct {
    int rows, cols, total_lines, hist_lines, scroll_top;
    int in_alt_screen;
    CHAR_INFO *buffer, *alt_buffer;
    WORD *fg_rgb, *bg_rgb, *alt_fg_rgb, *alt_bg_rgb;
    unsigned char *rgb_valid, *alt_rgb_valid;
    WORD current_attr;
} ScreenBuffer;

static inline int screen_phys_row(ScreenBuffer *s, int row) {
    if (row < 0) return 0;
    return (s->scroll_top + row) % s->total_lines;
}

static void screen_write_cell(ScreenBuffer *s, int row, int col, WCHAR ch, WORD attr) {
    int pr = screen_phys_row(s, row);
    int idx = pr * s->cols + col;
    s->buffer[idx].Char.UnicodeChar = ch;
    s->buffer[idx].Attributes = attr;
}

static void screen_scroll_up(ScreenBuffer *s, int top, int bottom, int count) {
    if (count <= 0) return;
    if (top < 0) top = 0;
    if (bottom >= s->rows) bottom = s->rows - 1;
    if (bottom < top) return;
    if (count > bottom - top + 1) count = bottom - top + 1;

    if (top == 0 && bottom == s->rows - 1) {
        s->hist_lines += count;
        if (s->hist_lines > SCROLL_BUF_LINES) s->hist_lines = SCROLL_BUF_LINES;
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

int main(void) {
    ScreenBuffer s;
    memset(&s, 0, sizeof(s));
    s.rows = 30;
    s.cols = 120;
    s.total_lines = s.rows + SCROLL_BUF_LINES;
    s.current_attr = 0x07;

    s.buffer = (CHAR_INFO *)calloc(s.total_lines * s.cols, sizeof(CHAR_INFO));
    s.fg_rgb = (WORD *)calloc(s.total_lines * s.cols, sizeof(WORD));
    s.bg_rgb = (WORD *)calloc(s.total_lines * s.cols, sizeof(WORD));
    s.rgb_valid = (unsigned char *)calloc(s.total_lines * s.cols, sizeof(unsigned char));
    assert(s.buffer && s.fg_rgb && s.bg_rgb && s.rgb_valid);

    // Test 1: simulate long output wrap-around (10025 line feeds)
    for (int i = 0; i < 10025; i++) {
        screen_scroll_up(&s, 0, s.rows - 1, 1);
    }
    assert(s.scroll_top == 10025 % s.total_lines);

    // Test 2: Partial scroll region at various cursor_y near wrap boundaries (1200 combinations)
    for (int wrap = 0; wrap < 40; wrap++) {
        s.scroll_top = (s.total_lines - 20 + wrap) % s.total_lines;
        for (int cy = 0; cy < s.rows; cy++) {
            for (int x = 0; x < s.cols; x++) {
                screen_write_cell(&s, cy, x, L'A' + (cy % 26), 0x07);
            }
            // CSI M with cursor_y = cy (top = cy, bottom = rows - 1)
            screen_scroll_up(&s, cy, s.rows - 1, 1);
        }
    }

    // Cleanup
    free(s.buffer);
    free(s.fg_rgb);
    free(s.bg_rgb);
    free(s.rgb_valid);

    printf("ASAN Ring Buffer verification passed: 0 overflows, 0 memory leaks.\n");
    return 0;
}
"""

def main():
    print("=== ASAN Ring Buffer Scrolling Test (verify_ringbuf_asan.py) ===")
    with tempfile.TemporaryDirectory() as tmpdir:
        c_path = os.path.join(tmpdir, "test_ringbuf.c")
        exe_path = os.path.join(tmpdir, "test_ringbuf")
        with open(c_path, "w", encoding="utf-8") as f:
            f.write(C_TEST_CODE)

        compile_cmd = [
            "gcc", "-O2", "-g", "-fsanitize=address,undefined",
            "-o", exe_path, c_path
        ]
        res = subprocess.run(compile_cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print("Compilation error:", res.stderr)
            return 1

        run_res = subprocess.run([exe_path], capture_output=True, text=True)
        if run_res.returncode != 0:
            print("Execution failed (ASAN error caught):\n", run_res.stderr)
            return 1
        print("  " + run_res.stdout.strip())
        print("  [OK] 环形缓冲区局部滚动区 (screen_scroll_up) 内存安全性验证通过！")
    return 0

if __name__ == "__main__":
    sys.exit(main())
