#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Exercise the production single-line input renderer at its cell edges.

This checks that get_input_screen_offset() and render_scrollable_input() agree
for empty, exact-fit, overflowing, CJK and emoji input, and that every cell
emitted for the input viewport is written while the supplied background is
active.  The functions are extracted from src/utf8.c so the test follows the
implementation rather than duplicating it.
"""

from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent
SOURCE = (ROOT / "src" / "utf8.c").read_text(encoding="utf-8")


def extract_func(text: str, prefix: str) -> str:
    start = text.find(prefix)
    if start < 0:
        raise RuntimeError(f"function not found: {prefix}")
    brace = text.find("{", start)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    raise RuntimeError(f"unterminated function: {prefix}")


FUNCTIONS = "\n\n".join(
    extract_func(SOURCE, prefix)
    for prefix in (
        "unsigned int utf8_decode_cp",
        "int is_zero_width_cp",
        "int is_wide_cp",
        "int utf8_cols",
        "int get_input_screen_offset",
        "void render_scrollable_input",
    )
)

PRELUDE = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef unsigned short WCHAR;
"""

DRIVER = r"""
static const char *BG = "\x1b[48;2;22;27;34m";

static void assert_background_cells(const char *out, int used, int expected_cells) {
    int bg_on = 0;
    int cells = 0;
    for (int i = 0; i < used; ) {
        if ((unsigned char)out[i] == 0x1b) {
            int end = i + 1;
            while (end < used && out[end] != 'm') end++;
            assert(end < used);
            if (strncmp(out + i, BG, strlen(BG)) == 0) bg_on = 1;
            if (strncmp(out + i, "\x1b[0m", 4) == 0) bg_on = 0;
            i = end + 1;
            continue;
        }
        int adv = 0;
        unsigned int cp = utf8_decode_cp(out + i, used - i, &adv);
        int width = is_zero_width_cp(cp) ? 0 : (is_wide_cp(cp) ? 2 : 1);
        assert(bg_on);
        cells += width;
        i += adv;
    }
    assert(cells == expected_cells);
    assert(bg_on == 0);
}

static void test_text(const char *text, int vis_width) {
    int len = (int)strlen(text);
    for (int cursor = 0; cursor <= len; ) {
        int adv = 1;
        if (cursor < len) {
            unsigned int cp = utf8_decode_cp(text + cursor, len - cursor, &adv);
            (void)cp;
        }

        char out[8192] = {0};
        int pos = 0;
        int screen = -1;
        render_scrollable_input(out, (int)sizeof(out), &pos, text, len, cursor,
                                vis_width, BG, &screen);
        int expected = get_input_screen_offset(text, len, cursor, vis_width);
        assert(screen == expected);
        assert(screen >= 0 && screen < vis_width);
        assert_background_cells(out, pos, vis_width);

        if (cursor == len) break;
        cursor += adv;
    }
}

int main(void) {
    const char *texts[] = {
        "",
        "a",
        "abcde",       /* exact narrow fit */
        "abcdef",      /* first overflow */
        "abcdefghijk",
        "中文a文b",     /* mixed wide/narrow */
        "中英文输入框",
        "😀ab😀cd",     /* emoji and overflow */
        "末尾   ",      /* trailing spaces */
        NULL
    };
    for (int i = 0; texts[i]; i++) {
        test_text(texts[i], 3);
        test_text(texts[i], 5);
        test_text(texts[i], 7);
    }
    puts("input layout regression passed: viewport background and cursor stay aligned.");
    return 0;
}
"""


def main() -> int:
    code = PRELUDE + "\n" + FUNCTIONS + "\n" + DRIVER
    with tempfile.TemporaryDirectory() as tmp:
        c_file = os.path.join(tmp, "input_layout.c")
        exe = os.path.join(tmp, "input_layout")
        Path(c_file).write_text(code, encoding="utf-8")
        result = subprocess.run(
            ["gcc", "-std=c99", "-O2", "-Wall", "-Wextra", c_file, "-o", exe],
            capture_output=True,
            text=True,
        )
        if result.returncode:
            print(result.stderr, file=sys.stderr)
            return 1
        result = subprocess.run([exe], capture_output=True, text=True)
        if result.returncode:
            print(result.stderr, file=sys.stderr)
            return 1
        print(result.stdout.strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
