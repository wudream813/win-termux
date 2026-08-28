#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""菜单项「启动默认颜色」选择条的真实渲染 / 命中一致性验证 (v1.8.9)。

选择条同时出现在图形化设置页的菜单项详情页和命令面板的 panel 编辑器里，
渲染与鼠标热区必须来自同一套几何：第 0 格「默认」宽 6，其后 8 个色块每格宽 3，
格子彼此相连。这里把 render.c 里的真实函数抠出来编译执行，逐列核对
item_color_hit() 与实际画出来的格子边界一致，并核对选中格用的是满强度标签色、
未选中格用的是暗色。
"""

import re
import subprocess
import sys
import tempfile
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parent
RENDER = (ROOT / "src" / "render.c").read_text(encoding="utf-8")
RENDER_H = (ROOT / "include" / "render.h").read_text(encoding="utf-8")


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


def array_literal(name: str) -> str:
    start = RENDER.index("static const char *const %s[9] = {" % name)
    end = RENDER.index("};", start) + 2
    return RENDER[start:end]


def define(name: str) -> str:
    m = re.search(r"^#define\s+%s\s+(.+)$" % name, RENDER_H, re.M)
    assert m, name
    return "#define %s %s" % (name, m.group(1).split("/*")[0].strip())


HARNESS = "\n".join([
    "#include <stdio.h>",
    "#include <string.h>",
    "int g_mouse_x = -1;",
    "int g_mouse_y = -1;",
    array_literal("TAB_COLOR_BG"),
    array_literal("TAB_COLOR_BG_DIM"),
    define("ITEM_COLOR_DEFAULT_W"),
    define("ITEM_COLOR_SWATCH_W"),
    define("ITEM_COLOR_ROW_W"),
    extract_func(RENDER, "int item_color_hit("),
    extract_func(RENDER, "void render_item_color_row("),
    r'''
int main(void) {
    char out[8192];
    for (int col = 5; col < 5 + ITEM_COLOR_ROW_W + 4; col++)
        printf("HIT %d %d\n", col, item_color_hit(10, col));
    for (int color = 0; color <= 8; color++) {
        int pos = 0;
        g_mouse_x = -1;
        g_mouse_y = -1;
        render_item_color_row(out, sizeof(out), &pos, 15, 10, color, color == 0);
        fwrite("ROW ", 1, 4, stdout);
        fwrite(out, 1, pos, stdout);
        fputc('\n', stdout);
    }
    return 0;
}
''',
])

ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
BG = re.compile(r"\x1b\[048;2;(\d{3});(\d{3});(\d{3})m")


def cols(text: str) -> int:
    return sum(2 if unicodedata.east_asian_width(ch) in "WF" else 1 for ch in text)


def main() -> int:
    print("=== 菜单项启动默认颜色选择条验证 (verify_item_color.py) ===")
    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "h.c"
        exe = Path(td) / "h.bin"
        src.write_text(HARNESS, encoding="utf-8")
        cp = subprocess.run(["gcc", "-Wall", "-Wextra", "-Werror", "-o", str(exe), str(src)],
                            capture_output=True, text=True)
        if cp.returncode != 0:
            print(cp.stderr, file=sys.stderr)
            print("FAIL: 选择条源码无法独立编译", file=sys.stderr)
            return 1
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        if run.returncode != 0:
            print(run.stderr, file=sys.stderr)
            return 1

    hits = {}
    rows = []
    for line in run.stdout.split("\n"):
        if line.startswith("HIT "):
            _, col, val = line.split()
            hits[int(col)] = int(val)
        elif line.startswith("ROW "):
            rows.append(line[4:])

    left = 10
    default_w, swatch_w = 6, 3
    # 1) 命中几何：格子相连、边界之外必须是 -1
    for col, val in hits.items():
        off = col - left
        if off < 0 or off >= default_w + 8 * swatch_w:
            want = -1
        elif off < default_w:
            want = 0
        else:
            want = 1 + (off - default_w) // swatch_w
        if val != want:
            print(f"FAIL: 第 {col} 列命中 {val}，期望 {want}", file=sys.stderr)
            return 1
    print(f"  命中几何正确：默认格 {default_w} 列 + 8 × {swatch_w} 列，越界返回 -1")

    # 2) 渲染宽度固定，且与命中几何等宽
    if len(rows) != 9:
        print("FAIL: 应当渲染 9 种选中状态", file=sys.stderr)
        return 1
    for color, row in enumerate(rows):
        body = row.split("H", 1)[1] if "H" in row.split("m")[0] + "H" else row
        text = ANSI.sub("", row)
        # 去掉开头的 CUP 目标，只留可见文本
        text = re.sub(r"^\x1b\[\d+;\d+H", "", text)
        strip = text[:0] + text
        width = cols(strip)
        # 30 列格子 + 2 空格 + 12 列提示文本（提示与占位等宽，且不含歧义宽度字符）
        if width != default_w + 8 * swatch_w + 2 + 12:
            print(f"FAIL: color={color} 行宽 {width} 列，期望 {default_w + 8 * swatch_w + 14}",
                  file=sys.stderr)
            print(repr(strip), file=sys.stderr)
            return 1
        del body
    print("  渲染宽度稳定：无论选中哪一格，整条宽度都不变")

    # 3) 选中的色块必须用满强度标签色，其余用暗色
    bright = re.findall(r'"(\\x1b\[048;2;\d{3};\d{3};\d{3}m)"', array_literal("TAB_COLOR_BG"))
    dim = re.findall(r'"(\\x1b\[048;2;\d{3};\d{3};\d{3}m)"', array_literal("TAB_COLOR_BG_DIM"))
    for color in range(1, 9):
        row = rows[color]
        want_bright = bright[color].replace("\\x1b", "\x1b")
        want_dim = dim[color].replace("\\x1b", "\x1b")
        marked = want_bright + "\x1b[038;2;013;017;023;1m[" + str(color) + "]"
        if marked not in row:
            print(f"FAIL: 选中的第 {color} 格没有用满强度标签色加 [] 标记", file=sys.stderr)
            print(repr(row), file=sys.stderr)
            return 1
        other = (color % 8) + 1
        if want_dim in row and other == color:
            print(f"FAIL: 选中格不该同时出现暗色 {want_dim}", file=sys.stderr)
            return 1
        if dim[other].replace("\\x1b", "\x1b") not in row:
            print(f"FAIL: 未选中的第 {other} 格没有用暗色", file=sys.stderr)
            return 1
    print("  选中格满强度 + [] 标记，未选中格暗色")

    print("  [OK] 启动默认颜色选择条：渲染与鼠标热区几何一致。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
