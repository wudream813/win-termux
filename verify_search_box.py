#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""搜索输入框的渲染几何验证 (v1.8.10)。

历史问题：搜索输入框以前铺满整条底行（`ui_bottom_row()`），而窗口一共只有
host_rows + 1 行、最后一行本来就是终端内容 —— 于是一按 Ctrl+B / 就凭空少一行
内容，用户看到的就是「搜索多一行」。现在它和搜索状态徽章一样是右上角的紧凑
小框。这里把真实的 render_search_box() 抠出来编译执行，断言：

  * 它只往 BADGE_ROW（第 2 行）写字，绝不碰最后一行；
  * 小框右边界正好贴着窗口右边缘，宽度固定为 SEARCH_BOX_COLS；
  * 光标列由同一个 search_box_layout() 得出，落在输入区内；
  * 关键词再长，框宽也不变（靠输入框自身滚动）。
"""

import re
import subprocess
import sys
import tempfile
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parent
RENDER = (ROOT / "src" / "render.c").read_text(encoding="utf-8")


def extract_func(text: str, signature: str) -> str:
    start = text.index(signature)
    i = text.index("{", start)
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[start:j + 1]
    raise AssertionError("unbalanced braces for " + signature)


def define(name: str) -> str:
    m = re.search(r"^#define\s+%s\s+(.+)$" % name, RENDER, re.M)
    assert m, name
    return "#define %s %s" % (name, m.group(1).split("/*")[0].strip())


HARNESS = "\n".join([
    "#include <stdio.h>",
    "#include <string.h>",
    '#include "utf8.h"',
    "char g_search_buf[64];",
    "int g_search_len = 0, g_search_pos = 0;",
    "int g_search_case_sensitive = 0;",
    define("BADGE_ROW"),
    define("SEARCH_BOX_PREFIX_COLS"),
    define("SEARCH_BOX_INPUT_COLS"),
    define("SEARCH_BOX_SUFFIX_COLS"),
    define("SEARCH_BOX_COLS"),
    extract_func(RENDER, "void search_box_layout("),
    extract_func(RENDER, "void render_search_box("),
    r'''
static void emit(int host_rows, int host_cols, const char *query) {
    char out[8192];
    int pos = 0;
    snprintf(g_search_buf, sizeof(g_search_buf), "%s", query);
    g_search_len = (int)strlen(g_search_buf);
    g_search_pos = g_search_len;
    render_search_box(out, sizeof(out), &pos, host_rows, host_cols);
    int row, left, input_col, input_w, scr_off;
    search_box_layout(host_cols, &row, &left, &input_col, &input_w);
    scr_off = get_input_screen_offset(g_search_buf, g_search_len, g_search_pos, input_w);
    printf("GEOM %d %d %d %d %d %d\n", host_rows, host_cols, row, left, input_col, input_w);
    printf("CURSOR %d\n", input_col + scr_off);
    fwrite("FRAME ", 1, 6, stdout);
    fwrite(out, 1, pos, stdout);
    fputc('\n', stdout);
}

int main(void) {
    emit(29, 120, "");
    emit(29, 120, "error");
    emit(29, 120, "aVeryLongSearchKeywordThatScrolls");
    emit(29, 120, "中文关键词也要能放下");
    emit(40, 80, "err");
    return 0;
}
''',
])

ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
CUP = re.compile(r"\x1b\[(\d+);(\d+)H")


def cols(text: str) -> int:
    return sum(2 if unicodedata.east_asian_width(ch) in "WF" else 1 for ch in text)


def main() -> int:
    print("=== 搜索输入框几何验证 (verify_search_box.py) ===")
    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "h.c"
        exe = Path(td) / "h.bin"
        src.write_text(HARNESS, encoding="utf-8")
        cp = subprocess.run(
            ["gcc", "-std=c99", "-O1", "-Wall", "-Wextra", "-Itests/stub", "-Iinclude",
             str(src), "src/utf8.c", "-o", str(exe)],
            cwd=ROOT, capture_output=True, text=True)
        if cp.returncode != 0:
            print(cp.stderr, file=sys.stderr)
            print("FAIL: 搜索小框源码无法独立编译", file=sys.stderr)
            return 1
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        if run.returncode != 0:
            print(run.stderr, file=sys.stderr)
            return 1

    geoms, cursors, frames = [], [], []
    for line in run.stdout.split("\n"):
        if line.startswith("GEOM "):
            geoms.append([int(x) for x in line.split()[1:]])
        elif line.startswith("CURSOR "):
            cursors.append(int(line.split()[1]))
        elif line.startswith("FRAME "):
            frames.append(line[6:])

    if not (len(geoms) == len(cursors) == len(frames) == 5):
        print("FAIL: harness 输出不完整", file=sys.stderr)
        return 1

    for (host_rows, host_cols, row, left, input_col, input_w), cursor, frame in zip(geoms, cursors, frames):
        # 1) 只写第 2 行，绝不碰最后一行内容
        rows = sorted({int(m.group(1)) for m in CUP.finditer(frame)})
        if rows != [2]:
            print(f"FAIL: 搜索框写到了第 {rows} 行，应当只写第 2 行", file=sys.stderr)
            return 1
        last_content_row = host_rows + 1
        if last_content_row in rows:
            print("FAIL: 搜索框又占用了最后一行终端内容", file=sys.stderr)
            return 1

        # 2) 宽度固定，右边界贴住窗口右缘
        text = ANSI.sub("", CUP.sub("", frame))
        width = cols(text)
        want = min(host_cols, 6 + 24 + 12)
        if width != want:
            print(f"FAIL: 小框宽 {width} 列，期望 {want} 列 -> {text!r}", file=sys.stderr)
            return 1
        if left + width - 1 != host_cols:
            print(f"FAIL: 小框右边界在第 {left + width - 1} 列，窗口宽 {host_cols}", file=sys.stderr)
            return 1

        # 3) 光标落在输入区内
        if not (input_col <= cursor <= input_col + input_w):
            print(f"FAIL: 光标列 {cursor} 落在输入区 [{input_col},{input_col + input_w}] 之外",
                  file=sys.stderr)
            return 1

    print(f"  {len(frames)} 组样例：只画第 2 行、宽度恒定 {geoms[0][5] + 18} 列、右对齐、光标在输入区内")
    print("  [OK] 搜索输入框不再吃掉整行终端内容。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
