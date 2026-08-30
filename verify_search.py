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

f_search = extract_func(src, "static void run_search(int live)")
if not f_search:
    sys.exit("FAIL: run_search not found in src/input.c")
# 保留对外的 execute_search()/search_preview_live() 薄封装，源码里它们转调 run_search。
f_search += "\nvoid execute_search(void) { run_search(0); }\n"
f_search += "\nvoid search_preview_live(void) { run_search(1); }\n"

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
int g_search_case_sensitive = 0;

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
    // v1.8.28：回车确认落在第 1 个匹配（cur=0）；D(next) 往下、U(prev) 往上。
    assert(g_search_match_cur == 0); // focus on first match

    // Test jump navigation: next = 下一个（行号增大），0->1->2->回0；prev 反之。
    search_jump_next(); // 下一个: 0 -> 1
    assert(g_search_match_cur == 1);
    search_jump_next(); // 下一个: 1 -> 2
    assert(g_search_match_cur == 2);
    search_jump_next(); // 回绕: 2 -> 0
    assert(g_search_match_cur == 0);
    search_jump_prev(); // 上一个: 0 -> 2（回绕到最后）
    assert(g_search_match_cur == 2);
    search_jump_prev(); // 上一个: 2 -> 1
    assert(g_search_match_cur == 1);

    // v1.8.7: search_case_sensitive 锁定大小写后，"error" 只应命中小写那两行，
    // "ERROR" 只应命中第 95 行。
    g_search_case_sensitive = 1;
    strcpy(g_search_buf, "error");
    g_search_len = (int)strlen(g_search_buf);
    execute_search();
    assert(g_search_match_count == 1);
    assert(g_search_matches[0].abs_y == 50);

    strcpy(g_search_buf, "ERROR");
    g_search_len = (int)strlen(g_search_buf);
    execute_search();
    assert(g_search_match_count == 1);
    assert(g_search_matches[0].abs_y == 95);

    strcpy(g_search_buf, "Error");
    g_search_len = (int)strlen(g_search_buf);
    execute_search();
    assert(g_search_match_count == 1);
    assert(g_search_matches[0].abs_y == 10);

    // 关掉锁定后仍然是三条
    g_search_case_sensitive = 0;
    strcpy(g_search_buf, "error");
    g_search_len = (int)strlen(g_search_buf);
    execute_search();
    assert(g_search_match_count == 3);

    // v1.8.26 实时预览（VSCode 式）：边打字边高亮，不滚动、不钉到最后一个匹配。
    {
        int before_vo = g_mux.panes[0].scroll_offset;
        g_search_mode = 1;
        g_search_active = 0; g_search_match_count = 0; g_search_match_cur = -1;
        strcpy(g_search_buf, "error");
        g_search_len = (int)strlen(g_search_buf);
        search_preview_live();          // live=1
        assert(g_search_active == 1);
        assert(g_search_match_count == 3);      // 全部匹配已算出来
        assert(g_search_match_cur == 0);        // 实时预览：指针置 0（不钉最后）
        assert(g_mux.panes[0].scroll_offset == before_vo);  // 视图不滚动
        // 关键词清空：实时预览也清掉高亮
        g_search_buf[0] = 0; g_search_len = 0;
        search_preview_live();
        assert(g_search_active == 0);
        assert(g_search_match_count == 0);
        g_search_mode = 0;
        // 恢复后重新跑一次正式搜索，供后续断言
        g_search_case_sensitive = 0;
        strcpy(g_search_buf, "error");
        g_search_len = (int)strlen(g_search_buf);
        execute_search();
    }

    free(s->lines[pr10].cells);
    free(s->lines[pr50].cells);
    free(s->lines[pr95].cells);
    free(s->lines);
    printf("Scrollback Search real source test passed successfully: 3/3 matches + 大小写锁定 + 实时预览(不滚动/计数/cur=0/清空) 验证通过。\n");
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

    # v1.8.26 源码不变量：
    # (1) 实时预览接线——搜索框编辑后必须调 search_preview_live()；
    # (2) 搜索徽章悬停不再展开重复的「U 上一个·D 下一个」长提示（按钮已常驻）。
    root = os.path.dirname(os.path.abspath(__file__))
    input_c = open(os.path.join(root, "src", "input.c"), encoding="utf-8").read()
    render_c = open(os.path.join(root, "src", "render.c"), encoding="utf-8").read()
    if "search_preview_live();" not in input_c:
        sys.exit("FAIL: 搜索框编辑后未调用 search_preview_live()（无实时预览）")
    # v1.8.29：搜索框内 Alt+C 手动切换区分大小写并实时重算。
    if "g_search_case_sensitive = !g_search_case_sensitive;" not in input_c.split("void handle_search_key")[1].split("void ")[0]:
        sys.exit("FAIL: handle_search_key 内没有 Alt+C 切换区分大小写")
    if 's_alt' not in input_c.split("void handle_search_key")[1].split("void ")[0]:
        sys.exit("FAIL: handle_search_key 未检测 Alt 修饰键（Alt+C 切换大小写）")
    # 搜索框后缀显示 Aa/aa 大小写状态标记。
    if 'Aa' not in render_c.split("void render_search_box")[1].split("void ")[0] or \
       'aa' not in render_c.split("void render_search_box")[1].split("void ")[0]:
        sys.exit("FAIL: 搜索框未显示 Aa/aa 大小写状态标记")
    # v1.8.29：copy_on_select 设置已删除。
    if "g_copy_on_select" in input_c or "copy_on_select" in render_c:
        sys.exit("FAIL: 已删除的 copy_on_select 设置仍有残留")
    # run_search 必须有 live 参数且 live 时不滚动
    if "static void run_search(int live)" not in input_c or "if (!live)" not in input_c:
        sys.exit("FAIL: run_search 缺少 live 参数 / live 时不应滚动")
    # 悬停展开只允许复制模式（b.kind == 1）；搜索（kind==2）不得再有 U 上一个长提示
    if "hovered && b.kind == 1" not in render_c:
        sys.exit("FAIL: 徽章悬停展开未限制为复制模式（搜索悬停提示应移除）")
    if "U 上一个 · D 下一个" in render_c:
        sys.exit("FAIL: 搜索徽章悬停仍展开重复的「U 上一个·D 下一个」长提示")
    print("  [OK] 实时预览已接线（编辑即高亮、不滚动）；搜索悬停重复提示已移除。")
    return 0

if __name__ == "__main__":
    sys.exit(main())
