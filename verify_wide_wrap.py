#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""verify_wide_wrap.py

v1.8.31 回归：宽字符（中文/全角）在「只剩最后一列」放不下而强制换行时，
旧行最后一列必须被清成普通空格，不能残留脏内容（旧字符、宽字次格 0 或主格）。

历史 bug：vt.c screen_put_cp 在 wide && cursor_x >= cols-1 时直接换行，
旧行末列（cursor_x == cols-1）没写任何东西。ConPTY 重绘时那一格会保留上一帧
脏内容——若恰好是宽字次格（ch==0 且左邻是宽字主格），snap_left_to_char 会把它
误判成宽字符次格而把选区左沿左退一列；块选经过这条「因汉字换行」的行之后，
后续窄字符行的高亮/复制整体错位一列。

本脚本链接【真实 src/screen.c + src/vt.c + src/utf8.c】，喂 UTF-8：先填满到
最后一列（cursor_x == cols-1），再喂一个汉字，断言旧行末列被清成空格。
变异：删掉 vt.c 换行分支里清末列的那行 screen_write_cell(... L' ' ...)，用例立即失败。
"""

import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, "src")
INC = os.path.join(ROOT, "include")
STUB = os.path.join(ROOT, "tests", "stub")

# 用 tests/stub 的 windows.h 替身；补齐 screen.c/vt.c 需要而 stub 未提供的类型。
STUB_EXTRA = r"""
typedef wchar_t WCHAR;
typedef struct { union { WCHAR UnicodeChar; char AsciiChar; } Char; unsigned short Attributes; } CHAR_INFO;
typedef struct { long dummy; } CRITICAL_SECTION;
typedef struct { unsigned long dwEventMask; } MOUSE_EVENT_RECORD;
#ifndef COMMON_LVB_UNDERSCORE
#define COMMON_LVB_UNDERSCORE 0x8000
#endif
#ifndef FOREGROUND_RED
#define FOREGROUND_RED 4
#define FOREGROUND_GREEN 2
#define FOREGROUND_BLUE 1
#define FOREGROUND_INTENSITY 8
#endif
"""

# types.h extern 的全局符号（screen.c/vt.c 引用）。
GLOBALS = r"""
#include "common.h"
#include "types.h"
int g_scrollback_lines = 10000;
MuxState g_mux;
int g_search_active;
int g_search_match_count;
int g_search_match_cur;
SearchMatch g_search_matches[MAX_SEARCH_MATCHES];
int g_search_case_sensitive;
"""

HARNESS = r"""
#include "screen.h"
#include "vt.h"
#include <stdio.h>

static int failures = 0;
static void ck(const char *n, int cond) {
    if (!cond) { printf("[FAIL] %s\n", n); failures++; }
    else       { printf("[ok]   %s\n", n); }
}

int main(void) {
    /* 宽 12 列。先写满 12 个窄字符（列0-11 都有真实内容，末列也是真实字符，
       模拟 ConPTY 重绘前的脏缓冲），此时 cursor 触发 wraparound_pending；
       再喂一个汉字：窄字符换行到第 2 行？不——更准的做法是直接把末列弄脏再让
       宽字在 cols-1 强制换行。做法：写 11 个窄字符到列0-10，手动把列11 写成
       脏宽字次格 0（像 ConPTY 半写残留），再喂汉字强制换行。 */
    ScreenBuffer s; screen_init(&s, 12, 6);
    screen_process_output(&s, "0123456789a", 11);   /* 列0-10，cursor=11=cols-1 */
    screen_write_cell(&s, 0, 11, 0, 7);              /* 末列脏残格：宽字次格 0 */
    screen_process_output(&s, "\xe6\xb1\x89", 3);   /* 汉 U+6C49 强制换行 */

    CHAR_INFO *last_prev = screen_cell(&s, 0, 11);
    CHAR_INFO *new0 = screen_cell(&s, 1, 0);
    CHAR_INFO *new1 = screen_cell(&s, 1, 1);
    ck("宽字换行：旧行末列(列11)被清成空格", last_prev && last_prev->Char.UnicodeChar == L' ');
    ck("宽字换行：新行列0 是汉字主格", new0 && new0->Char.UnicodeChar == 0x6C49);
    ck("宽字换行：新行列1 是次格占位0", new1 && new1->Char.UnicodeChar == 0);
    /* 旧行末列绝不能是宽字次格（ch==0），否则 snap 会把它误当宽字次格左退一列。 */
    ck("宽字换行：旧行末列不是宽字次格(0)", last_prev && last_prev->Char.UnicodeChar != 0);

    /* 同样验证 cursor 停在 cols-2（宽字要写 cols-2/cols-1 正好放下）时不应误清：
       写 10 个窄字符（cursor 到 10 == cols-2），汉字占列10/11 正好，不换行。 */
    ScreenBuffer s2; screen_init(&s2, 12, 4);
    screen_process_output(&s2, "0123456789", 10);
    screen_process_output(&s2, "\xe6\xb1\x89", 3);
    CHAR_INFO *f10 = screen_cell(&s2, 0, 10);
    CHAR_INFO *f11 = screen_cell(&s2, 0, 11);
    ck("正好放下：汉字主格在列10", f10 && f10->Char.UnicodeChar == 0x6C49);
    ck("正好放下：汉字次格在列11(0占位)", f11 && f11->Char.UnicodeChar == 0);

    if (failures) { printf("\n%d FAILURE(S)\n", failures); return 1; }
    printf("\nWIDE-WRAP CHECKS PASSED\n");
    return 0;
}
"""


def main() -> int:
    print("=== 宽字符强制换行清旧行末格验证 (verify_wide_wrap.py) ===")
    with tempfile.TemporaryDirectory() as td:
        td = "/tmp/wrap_chk"
        os.makedirs(td, exist_ok=True)
        # 组装一个含补充类型的 stub windows.h。
        stub_win = os.path.join(STUB, "windows.h")
        with open(stub_win, encoding="utf-8") as f:
            stub_txt = f.read()
        # 保证 WCHAR / CHAR_INFO 等存在（stub 已含 WCHAR；仅追加缺的类型，避免重复定义）。
        extra = ""
        if "CHAR_INFO" not in stub_txt:
            extra += STUB_EXTRA
        with open(os.path.join(td, "windows.h"), "w", encoding="utf-8") as f:
            f.write(stub_txt + "\n" + extra)
        for name in ("shellapi.h", "process.h"):
            srcp = os.path.join(STUB, name)
            if os.path.exists(srcp):
                with open(srcp, encoding="utf-8") as f, open(os.path.join(td, name), "w", encoding="utf-8") as o:
                    o.write(f.read())
        with open(os.path.join(td, "globs.c"), "w", encoding="utf-8") as f:
            f.write(GLOBALS)
        with open(os.path.join(td, "h.c"), "w", encoding="utf-8") as f:
            f.write(HARNESS)
        exe = os.path.join(td, "h.bin")
        cp = subprocess.run(
            ["gcc", "-O1", "-Wall", "-Wextra", "-I" + td, "-I" + INC,
             os.path.join(td, "h.c"), os.path.join(td, "globs.c"),
             os.path.join(SRC, "screen.c"), os.path.join(SRC, "vt.c"),
             os.path.join(SRC, "utf8.c"), "-o", exe, "-lm"],
            capture_output=True, text=True)
        if cp.returncode != 0:
            print(cp.stderr, file=sys.stderr)
            print("FAIL: 宽字符换行 harness 无法编译", file=sys.stderr)
            return 1
        run = subprocess.run([exe], capture_output=True, text=True)
        print(run.stdout)
        if run.returncode != 0 or "[FAIL]" in run.stdout:
            print(run.stderr, file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
