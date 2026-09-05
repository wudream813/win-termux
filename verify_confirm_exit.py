#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Rendered-geometry regression for the exit confirmation dialog (v1.8.5).

The dialog used to be written as hand-counted literal strings, so the option
row was six columns short and its right border landed inside the box.  This
harness compiles the real renderer functions out of src/render.c, renders the
dialog, and asserts that every one of its four rows is exactly CONFIRM_W
columns wide with the border characters in the same column.  It also checks
that the two buttons are highlighted exactly where confirm_exit_button_geom
says they are clickable.
"""

import os
import re
import subprocess
import sys
import tempfile
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parent
RENDER = (ROOT / "src" / "render.c").read_text(encoding="utf-8")


def extract_func(text: str, signature: str) -> str:
    """Return the definition (never a forward declaration) starting at signature."""
    start = -1
    probe = 0
    while True:
        start = text.index(signature, probe)
        close = text.index(")", text.index("(", start))
        if text[close + 1:close + 40].lstrip().startswith("{"):
            break
        probe = start + 1
    depth = 0
    i = text.index("{", start)
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[start:j + 1]
    raise AssertionError("unbalanced braces for " + signature)


def defines(names):
    out = []
    for name in names:
        m = re.search(r"^#define\s+%s\s+(-?\d+)" % name, RENDER, re.M)
        assert m, name
        out.append("#define %s %s" % (name, m.group(1)))
    return "\n".join(out)


HARNESS = "\n".join([
    '#include <stdio.h>',
    '#include <string.h>',
    '#include "utf8.h"',
    'int g_mouse_x = -1;',
    'int g_mouse_y = -1;',
    defines(["CONFIRM_W", "CONFIRM_H", "CONFIRM_YES_W", "CONFIRM_NO_W", "CONFIRM_GAP"]),
    extract_func(RENDER, "static void palette_hline("),
    extract_func(RENDER, "void confirm_exit_geom("),
    extract_func(RENDER, "void confirm_exit_button_geom("),
    extract_func(RENDER, "static void confirm_pad("),
    extract_func(RENDER, "void render_confirm_dialog("),
    extract_func(RENDER, "void render_confirm_exit("),
    r'''
static void dump(int rows, int cols, int mx, int my) {
    char out[16384];
    int pos = 0;
    g_mouse_x = mx;
    g_mouse_y = my;
    render_confirm_exit(out, sizeof(out), &pos, rows, cols);
    fwrite(out, 1, pos, stdout);
    fputc('\n', stdout);
}

int main(void) {
    int row, ys, ye, ns, ne, top, left, w, h;
    confirm_exit_geom(30, 100, &top, &left, &w, &h);
    confirm_exit_button_geom(30, 100, &row, &ys, &ye, &ns, &ne);
    printf("GEOM %d %d %d %d %d %d %d %d %d\n", top, left, w, h, row, ys, ye, ns, ne);
    dump(30, 100, -1, -1);          /* no hover */
    dump(30, 100, ys - 1, row - 1); /* hovering the confirm button */
    dump(30, 100, ns - 1, row - 1); /* hovering the cancel button */
    return 0;
}
''',
])

ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
CUP = re.compile(r"\x1b\[(\d+);(\d+)H")


def cols(text: str) -> int:
    return sum(2 if unicodedata.east_asian_width(ch) in "WF" else 1 for ch in text)


def rows_of(dump: str):
    """Split one rendered frame into (row, start_col, visible_text) tuples."""
    out = []
    parts = CUP.split(dump)
    # parts = [prefix, row, col, chunk, row, col, chunk, ...]
    for i in range(1, len(parts), 3):
        row, col, chunk = int(parts[i]), int(parts[i + 1]), parts[i + 2]
        out.append((row, col, ANSI.sub("", chunk)))
    return out


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "confirm.c")
        exe = os.path.join(tmp, "confirm")
        Path(src).write_text(HARNESS, encoding="utf-8")
        build = subprocess.run(
            ["gcc", "-std=c99", "-O1", "-Wall", "-Wextra", "-Itests/stub", "-Iinclude",
             src, "src/utf8.c", "-o", exe],
            cwd=ROOT, capture_output=True, text=True)
        if build.returncode:
            print(build.stderr or build.stdout, file=sys.stderr)
            return 1
        run = subprocess.run([exe], cwd=ROOT, capture_output=True, text=True)
        if run.returncode:
            print(run.stderr or run.stdout, file=sys.stderr)
            return 1

    lines = run.stdout.splitlines()
    geom = lines[0].split()
    top, left, w, _h, row, ys, ye, ns, ne = (int(v) for v in geom[1:])
    frames = [rows_of(line) for line in lines[1:4]]

    errors = []
    for name, frame in zip(("无 hover", "hover 确认", "hover 取消"), frames):
        if len(frame) != 4:
            errors.append(f"{name}: 对话框渲染了 {len(frame)} 行（应为 4 行）")
            continue
        for index, (r, c, text) in enumerate(frame):
            if r != top + index or c != left:
                errors.append(f"{name}: 第 {index + 1} 行定位到 ({r},{c})，应为 ({top + index},{left})")
            width = cols(text)
            if width != w:
                errors.append(f"{name}: 第 {index + 1} 行宽 {width} 列（应为 {w} 列）：{text!r}")
            if not text.startswith(("┌", "│", "└")) or not text.endswith(("┐", "│", "┘")):
                errors.append(f"{name}: 第 {index + 1} 行边框字符缺失：{text!r}")

    # The option row must place the buttons exactly where the mouse handler
    # looks for them.
    option = frames[0][2][2]

    def index_at_col(text, offset):
        """String index of the character starting at display column `offset`."""
        seen = 0
        for i, ch in enumerate(text):
            if seen == offset:
                return i
            seen += cols(ch)
        return len(text)

    yes_off = index_at_col(option, ys - left)
    no_off = index_at_col(option, ns - left)
    if not option[yes_off:].startswith("[ Y 确认 ]"):
        errors.append(f"确认按钮不在 confirm_exit_button_geom 报出的列 {ys}：{option!r}")
    if not option[no_off:].startswith("[ N/Esc 取消 ]"):
        errors.append(f"取消按钮不在 confirm_exit_button_geom 报出的列 {ns}：{option!r}")
    if ye - ys != cols("[ Y 确认 ]") or ne - ns != cols("[ N/Esc 取消 ]"):
        errors.append("按钮热区宽度与实际绘制宽度不一致")

    # Hovering must repaint that button with a filled background, and only it.
    def filled(frame_index):
        raw = lines[1 + frame_index]
        return raw.count("\x1b[048;2;248;081;073m"), raw.count("\x1b[048;2;063;185;080m")

    if filled(0) != (1, 0):  # header keeps its red background, no button filled
        errors.append("未 hover 时按钮不应有填充背景")
    if filled(1)[0] != 2:
        errors.append("hover 确认按钮时没有整块高亮")
    if filled(2)[1] != 1:
        errors.append("hover 取消按钮时没有整块高亮")

    if errors:
        print("退出确认对话框验证失败:", file=sys.stderr)
        for e in errors:
            print("  FAIL:", e, file=sys.stderr)
        return 1

    print("退出确认对话框验证通过：4 行等宽 %d 列，按钮热区与高亮一致（Y@%d-%d, N@%d-%d）"
          % (w, ys, ye, ns, ne))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
