#!/usr/bin/env python3
"""verify_copy_snap.py

v1.8.19 回归：选区端点必须吸附到完整字符，避免选中半个宽字符（中文/emoji）。

宽字符占两列：
  - BMP 宽字符（中文/全角/假名）：主格写字、次格写 0 占位；
  - non-BMP emoji：主格高代理、次格低代理。
端点落在宽字符中间时：
  - 右端点在主格 -> 右扩 1（snap_right_to_char）；
  - 左端点在次格 -> 左退 1（snap_left_to_char）。

从【真实 src/screen.c】抽取两个函数，构造一行 WCHAR 做边界断言。
变异：把 snap_*_to_char 改成恒返回原 x（不吸附），用例立即失败。
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.abspath(__file__))
SCREEN_C = os.path.join(ROOT, "src", "screen.c")
SRC = os.path.join(ROOT, "src")
INC = os.path.join(ROOT, "include")

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include <wchar.h>
typedef unsigned short WORD; typedef unsigned short WCHAR;
typedef unsigned int UINT; typedef void* HANDLE; typedef int BOOL;
int is_wide_cp(unsigned int cp);
%(funcs)s

static int failures = 0;
static void ck(const char *n, int got, int want) {
    if (got != want) { printf("[FAIL] %s: got %d want %d\n", n, got, want); failures++; }
    else             { printf("[ok]   %s: %d\n", n, got); }
}

int main(void) {
    /* "ab 保 x"：
       col 0 'a', 1 'b', 2 ' ', 3 保(0x4FDD 主格), 4 保占位0(次格),
       5 'x'。中文占 3、4 两列。 */
    WCHAR line1[16] = { 'a','b',' ',0x4FDD,0,'x', 0,0,0,0,0,0,0,0,0,0 };
    int n = 16;

    /* 右端点：在中文主格 col3 -> 扩到 col4（包含占位） */
    ck("右:端点在中文主格(3)->扩到4", snap_right_to_char(line1,n,3), 4);
    /* 右端点在中文次格 col4 -> 已是次格，不再扩（次格不宽）-> 保持 4 */
    ck("右:端点在中文次格(4)->不扩", snap_right_to_char(line1,n,4), 4);
    /* 右端点在 ASCII col1 -> 不变 */
    ck("右:端点在ASCII(1)->不变", snap_right_to_char(line1,n,1), 1);
    /* 右端点在空格 col2 -> 不变 */
    ck("右:端点在空格(2)->不变", snap_right_to_char(line1,n,2), 2);

    /* 左端点：落在中文次格 col4 -> 左退到主格 col3 */
    ck("左:端点在中文次格(4)->退到3", snap_left_to_char(line1,n,4), 3);
    /* 左端点在中文主格 col3 -> 不变 */
    ck("左:端点在中文主格(3)->不变", snap_left_to_char(line1,n,3), 3);
    /* 左端点在 ASCII col5('x') -> 不变 */
    ck("左:端点在ASCII x(5)->不变", snap_left_to_char(line1,n,5), 5);
    /* 左端点在 col0 -> 保持 0（不能负） */
    ck("左:端点col0->0", snap_left_to_char(line1,n,0), 0);

    /* emoji 代理对：😀 U+1F600 = 高代理 0xD83D, 低代理 0xDE00，占 col6 主、col7 次 */
    WCHAR line2[16]; memset(line2,0,sizeof line2);
    line2[0]='A'; line2[1]=0xD83D; line2[2]=0xDE00; line2[3]='B';
    ck("右:端点在emoji高代理(1)->扩到2", snap_right_to_char(line2,n,1), 2);
    ck("右:端点在emoji低代理(2)->不扩", snap_right_to_char(line2,n,2), 2);
    ck("左:端点在emoji低代理(2)->退到1", snap_left_to_char(line2,n,2), 1);
    ck("左:端点在emoji高代理(1)->不变", snap_left_to_char(line2,n,1), 1);

    /* 全角字符 '＠' U+FF20 宽 */
    WCHAR line3[8] = { 'x', 0xFF20, 0, 'y', 0,0,0,0 };
    ck("右:全角主格(1)->扩到2", snap_right_to_char(line3,8,1), 2);
    ck("左:全角次格(2)->退到1", snap_left_to_char(line3,8,2), 1);

    /* 越界夹紧：宽主格在最后一列、无次格可扩 -> 不越界 */
    WCHAR line4[4] = {'a','b','c',0x4FDD};
    ck("右:末列宽主格无次格->不越界(3)", snap_right_to_char(line4,4,3), 3);

    if (failures) { printf("\n%d FAILURE(S)\n", failures); return 1; }
    printf("\nALL SNAP-TO-CHAR CHECKS PASSED\n");
    return 0;
}
"""


def extract_func(src, name):
    idx = src.find(name)
    if idx < 0:
        raise SystemExit("找不到 " + name)
    # 回退到行首（int name( ）
    ls = src.rfind("\n", 0, idx) + 1
    brace = src.find("{", idx)
    depth = 0
    i = brace
    while i < len(src):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[ls:i + 1]
        i += 1
    raise SystemExit("花括号不配对 " + name)


def main():
    src = open(SCREEN_C, encoding="utf-8").read()
    f1 = extract_func(src, "snap_right_to_char")
    f2 = extract_func(src, "snap_left_to_char")
    harness = HARNESS.replace("%(funcs)s", f1 + "\n" + f2)

    with tempfile.TemporaryDirectory() as td:
        stub = os.path.join(td, "stub")
        os.makedirs(stub)
        winh = os.path.join(stub, "windows.h")
        open(winh, "w").write(
            "#pragma once\n#include <stddef.h>\n#include <wchar.h>\n"
            "typedef unsigned short WORD; typedef unsigned long DWORD; typedef long LONG;\n"
            "typedef short SHORT; typedef wchar_t WCHAR; typedef unsigned char BYTE;\n"
            "typedef unsigned int UINT; typedef int INT; typedef void* HANDLE; typedef int BOOL;\n"
            "#define TRUE 1\n#define FALSE 0\n#ifndef NULL\n#define NULL 0\n#endif\n")
        for h in ["shellapi.h", "process.h", "windowsx.h"]:
            open(os.path.join(stub, h), "w").write('#pragma once\n#include "windows.h"\n')

        hc = os.path.join(td, "h.c")
        binp = os.path.join(td, "h")
        open(hc, "w", encoding="utf-8").write(harness)
        uo = os.path.join(td, "utf8.o")
        p = subprocess.run(["gcc", "-O1", "-g", "-fsanitize=address,undefined",
                            "-I", stub, "-I", INC, "-c",
                            os.path.join(SRC, "utf8.c"), "-o", uo],
                           capture_output=True, text=True)
        if p.returncode != 0:
            print(p.stderr); raise SystemExit("utf8 编译失败")
        p = subprocess.run(["gcc", "-O1", "-g", "-fsanitize=address,undefined",
                            "-Wall", "-Wextra", "-Werror", "-I", INC,
                            hc, uo, "-o", binp],
                           capture_output=True, text=True)
        if p.returncode != 0:
            print(p.stdout); print(p.stderr); raise SystemExit("编译失败")
        r = subprocess.run([binp], capture_output=True, text=True,
                           env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
        print(r.stdout)
        if r.returncode != 0:
            print(r.stderr); raise SystemExit(1)
    print("[OK] verify_copy_snap passed")


if __name__ == "__main__":
    main()
