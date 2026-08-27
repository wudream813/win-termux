#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_search.py - Real Source Verification for Scrollback History Search.
Dynamically extracts execute_search, search_jump_next, and search_jump_prev from src/input.c,
compiles with GCC and tests substring search, case-insensitivity, jump navigation, and line addressing.
"""

import subprocess
import tempfile
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
src = (ROOT / "src" / "input.c").read_text(encoding="utf-8")

def extract_func(text, prefix):
    idx = text.find(prefix)
    if idx == -1:
        return None
    brace_start = text.find("{", idx)
    if brace_start == -1:
        return None
    depth = 0
    for i in range(brace_start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[idx : i + 1]
    return None

f_search = extract_func(src, "void execute_search(void)")
if not f_search:
    sys.exit("FAIL: execute_search not found in src/input.c")

f_next = extract_func(src, "void search_jump_next(void)")
if not f_next:
    sys.exit("FAIL: search_jump_next not found in src/input.c")

f_prev = extract_func(src, "void search_jump_prev(void)")
if not f_prev:
    sys.exit("FAIL: search_jump_prev not found in src/input.c")

PRELUDE = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <assert.h>

#define SCROLL_BUF_LINES 10000
#define MAX_PANES 16
#define MAX_SEARCH_MATCHES 2048
#define CP_UTF8 65001

typedef unsigned short WORD;
typedef unsigned short WCHAR;
typedef void* CRITICAL_SECTION;

typedef struct {
    union {
        WCHAR UnicodeChar;
        char   AsciiChar;
    } Char;
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
    int is_settings;
    int is_about;
    int exited_hold;
    unsigned long exit_code;
    WCHAR input_history[256];
    int input_history_len;
    int input_history_pos;
} Pane;

typedef struct {
    int abs_y;
    int start_x;
    int end_x;
} SearchMatch;

typedef struct {
    Pane panes[MAX_PANES];
    int pane_count, active_pane;
    CRITICAL_SECTION cs;
    int needs_redraw;
} MuxState;

MuxState g_mux;
SearchMatch g_search_matches[MAX_SEARCH_MATCHES];
int g_search_match_count = 0;
int g_search_match_cur = -1;
int g_search_mode = 0;
int g_search_active = 0;
char g_search_buf[64] = {0};
int g_search_len = 0, g_search_pos = 0;

static inline void EnterCriticalSection(void *cs) { (void)cs; }
static inline void LeaveCriticalSection(void *cs) { (void)cs; }

static inline int MultiByteToWideChar(unsigned int cp, unsigned long flags, const char *src, int src_len, WCHAR *dst, int dst_len) {
    (void)cp; (void)flags;
    if (src_len < 0) src_len = (int)strlen(src);
    int count = 0;
    for (int i = 0; i < src_len && count < dst_len; i++) {
        dst[count++] = (unsigned char)src[i];
    }
    return count;
}
"""

DRIVER = r"""
int main(void) {
    memset(&g_mux, 0, sizeof(g_mux));
    g_mux.pane_count = 1;
    g_mux.active_pane = 0;
    Pane *p = &g_mux.panes[0];
    p->active = 1;
    ScreenBuffer *s = &p->screen;
    s->rows = 20;
    s->cols = 80;
    s->total_lines = s->rows + SCROLL_BUF_LINES;
    s->hist_lines = 100;
    s->scroll_top = 50;

    s->lines = (ScreenLine *)calloc(s->total_lines, sizeof(ScreenLine));
    assert(s->lines);

    // Populate lines with test text
    // Line 10: "Error: file not found"
    // Line 50: "warning: error in line 42"
    // Line 95: "ERROR: critical failure"
    const char *l10 = "Error: file not found";
    const char *l50 = "warning: error in line 42";
    const char *l95 = "ERROR: critical failure";

    int ar10 = 10, ar50 = 50, ar95 = 95;
    int pr10 = (s->scroll_top - s->hist_lines + ar10 + s->total_lines * 2) % s->total_lines;
    int pr50 = (s->scroll_top - s->hist_lines + ar50 + s->total_lines * 2) % s->total_lines;
    int pr95 = (s->scroll_top - s->hist_lines + ar95 + s->total_lines * 2) % s->total_lines;

    s->lines[pr10].cells = (CHAR_INFO *)calloc(s->cols, sizeof(CHAR_INFO));
    s->lines[pr50].cells = (CHAR_INFO *)calloc(s->cols, sizeof(CHAR_INFO));
    s->lines[pr95].cells = (CHAR_INFO *)calloc(s->cols, sizeof(CHAR_INFO));

    for (int i = 0; i < (int)strlen(l10); i++) s->lines[pr10].cells[i].Char.UnicodeChar = l10[i];
    for (int i = 0; i < (int)strlen(l50); i++) s->lines[pr50].cells[i].Char.UnicodeChar = l50[i];
    for (int i = 0; i < (int)strlen(l95); i++) s->lines[pr95].cells[i].Char.UnicodeChar = l95[i];

    // Search for "error" (case-insensitive)
    strcpy(g_search_buf, "error");
    g_search_len = strlen(g_search_buf);
    execute_search();

    assert(g_search_active == 1);
    assert(g_search_match_count == 3);
    assert(g_search_matches[0].abs_y == 10 && g_search_matches[0].start_x == 0);
    assert(g_search_matches[1].abs_y == 50 && g_search_matches[1].start_x == 9);
    assert(g_search_matches[2].abs_y == 95 && g_search_matches[2].start_x == 0);
    assert(g_search_match_cur == 2); // Default focus on latest match

    // Test jump navigation
    search_jump_next(); // Should wrap to 1 (prev earlier match)
    assert(g_search_match_cur == 1);
    search_jump_next(); // Should wrap to 0
    assert(g_search_match_cur == 0);
    search_jump_prev(); // Should go forward to 1
    assert(g_search_match_cur == 1);

    free(s->lines[pr10].cells);
    free(s->lines[pr50].cells);
    free(s->lines[pr95].cells);
    free(s->lines);
    printf("Scrollback Search real source test passed successfully: 3/3 matches found and verified.\n");
    return 0;
}
"""

C_SEARCH_TEST_CODE = PRELUDE + "\n" + f_search + "\n" + f_next + "\n" + f_prev + "\n" + DRIVER

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
        print("  [OK] 滚动历史搜索 (Scrollback History Search) 真实源码验证通过！")
    return 0

if __name__ == "__main__":
    sys.exit(main())
