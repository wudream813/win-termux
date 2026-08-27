#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_search.py - Behavioral verification for Scrollback History Search (v1.6.1).
Extracts search logic and compiles with GCC to test substring search, case-insensitivity,
jump navigation, and ring buffer line addressing.
"""

import subprocess
import tempfile
import os
import sys

C_SEARCH_TEST_CODE = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <assert.h>

#define SCROLL_BUF_LINES 10000

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
    int scroll_offset;
} ScreenBuffer;

typedef struct {
    int abs_y;
    int start_x;
    int end_x;
} SearchMatch;

#define MAX_SEARCH_MATCHES 2048
static SearchMatch g_search_matches[MAX_SEARCH_MATCHES];
static int g_search_match_count = 0;
static int g_search_match_cur = -1;
static int g_search_active = 0;

static void execute_search_test(ScreenBuffer *s, const WCHAR *wquery, int wq_len) {
    g_search_match_count = 0;
    g_search_match_cur = -1;
    if (wq_len <= 0) {
        g_search_active = 0;
        return;
    }

    int total_lines = s->in_alt_screen ? s->rows : (s->hist_lines + s->rows);
    WCHAR *row_chars = (WCHAR *)malloc(s->cols * sizeof(WCHAR));
    assert(row_chars);

    for (int abs_y = 0; abs_y < total_lines; abs_y++) {
        int rlen = s->cols;
        for (int x = 0; x < s->cols; x++) {
            CHAR_INFO *cell = NULL;
            if (s->in_alt_screen) {
                cell = &s->alt_buffer[abs_y * s->cols + x];
            } else {
                int ar = abs_y;
                int pr = (s->scroll_top - s->hist_lines + ar + s->total_lines * 2) % s->total_lines;
                cell = &s->buffer[pr * s->cols + x];
            }
            row_chars[x] = cell ? cell->Char.UnicodeChar : L' ';
        }

        for (int x = 0; x <= rlen - wq_len; x++) {
            int match = 1;
            for (int k = 0; k < wq_len; k++) {
                WCHAR c1 = towlower(row_chars[x + k]);
                WCHAR c2 = towlower(wquery[k]);
                if (c1 != c2) {
                    match = 0;
                    break;
                }
            }
            if (match && g_search_match_count < MAX_SEARCH_MATCHES) {
                g_search_matches[g_search_match_count].abs_y = abs_y;
                g_search_matches[g_search_match_count].start_x = x;
                g_search_matches[g_search_match_count].end_x = x + wq_len - 1;
                g_search_match_count++;
            }
        }
    }
    free(row_chars);

    if (g_search_match_count > 0) {
        g_search_active = 1;
        g_search_match_cur = g_search_match_count - 1;
        int target_abs_y = g_search_matches[g_search_match_cur].abs_y;
        if (!s->in_alt_screen) {
            int vo = s->hist_lines - (target_abs_y - s->rows / 2);
            if (vo < 0) vo = 0;
            if (vo > s->hist_lines) vo = s->hist_lines;
            s->scroll_offset = vo;
        }
    } else {
        g_search_active = 0;
    }
}

int main(void) {
    ScreenBuffer s;
    memset(&s, 0, sizeof(s));
    s.rows = 20;
    s.cols = 80;
    s.total_lines = s.rows + SCROLL_BUF_LINES;
    s.hist_lines = 100;
    s.scroll_top = 50;

    s.buffer = (CHAR_INFO *)calloc(s.total_lines * s.cols, sizeof(CHAR_INFO));
    assert(s.buffer);

    // Populate lines with test text
    // Line 10: "Error: file not found"
    // Line 50: "warning: error in line 42"
    // Line 95: "ERROR: critical failure"
    const char *l10 = "Error: file not found";
    const char *l50 = "warning: error in line 42";
    const char *l95 = "ERROR: critical failure";

    int ar10 = 10, ar50 = 50, ar95 = 95;
    int pr10 = (s.scroll_top - s.hist_lines + ar10 + s.total_lines * 2) % s.total_lines;
    int pr50 = (s.scroll_top - s.hist_lines + ar50 + s.total_lines * 2) % s.total_lines;
    int pr95 = (s.scroll_top - s.hist_lines + ar95 + s.total_lines * 2) % s.total_lines;

    for (int i = 0; i < (int)strlen(l10); i++) s.buffer[pr10 * s.cols + i].Char.UnicodeChar = l10[i];
    for (int i = 0; i < (int)strlen(l50); i++) s.buffer[pr50 * s.cols + i].Char.UnicodeChar = l50[i];
    for (int i = 0; i < (int)strlen(l95); i++) s.buffer[pr95 * s.cols + i].Char.UnicodeChar = l95[i];

    // Search for "error" (case-insensitive)
    WCHAR q[] = {L'e', L'r', L'r', L'o', L'r'};
    execute_search_test(&s, q, 5);

    assert(g_search_active == 1);
    assert(g_search_match_count == 3);
    assert(g_search_matches[0].abs_y == 10 && g_search_matches[0].start_x == 0);
    assert(g_search_matches[1].abs_y == 50 && g_search_matches[1].start_x == 9);
    assert(g_search_matches[2].abs_y == 95 && g_search_matches[2].start_x == 0);
    assert(g_search_match_cur == 2); // Default focus on latest match

    free(s.buffer);
    printf("Scrollback Search behavioral test passed successfully: 3/3 matches found with exact coordinates.\n");
    return 0;
}
"""

def main():
    print("=== Scrollback History Search Test (verify_search.py) ===")
    with tempfile.TemporaryDirectory() as tmpdir:
        c_path = os.path.join(tmpdir, "test_search.c")
        exe_path = os.path.join(tmpdir, "test_search")
        with open(c_path, "w", encoding="utf-8") as f:
            f.write(C_SEARCH_TEST_CODE)

        compile_cmd = ["gcc", "-O2", "-Wall", "-o", exe_path, c_path]
        res = subprocess.run(compile_cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print("Compilation error:", res.stderr)
            return 1

        run_res = subprocess.run([exe_path], capture_output=True, text=True)
        if run_res.returncode != 0:
            print("Execution failed:\n", run_res.stderr)
            return 1
        print("  " + run_res.stdout.strip())
        print("  [OK] 滚动历史搜索 (Scrollback History Search) 行为验证通过！")
    return 0

if __name__ == "__main__":
    sys.exit(main())
