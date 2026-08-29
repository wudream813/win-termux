#!/usr/bin/env python3
"""verify_copy_quantize.py

v1.8.21 回归：复制模式里【拖动鼠标】时光标按整字跳动，g_copy_cx 永不停在
宽字符（中文/全角/emoji）的次格（半个字）上。

copy_quantize_cursor(line, ncols, x, anchor_x)：
  - 向右扩展（x >= anchor_x）：指针落在宽字符【主格】-> 跳到次格（一次跨过
    整个汉字/emoji）；落在次格或窄字符 -> 原样；
  - 向左扩展（x <  anchor_x）：指针落在宽字符【次格】-> 退到主格。

从【真实 src/screen.c】抽取 copy_quantize_cursor() 与其依赖的
cell_is_wide_trail() 编译断言。变异：把整字化改成恒返回 x（旧行为）即失败。
"""
import os
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.abspath(__file__))
SCREEN_C = os.path.join(ROOT, "src", "screen.c")
SRC = os.path.join(ROOT, "src")
INC = os.path.join(ROOT, "include")

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include <wchar.h>
typedef unsigned short WCHAR;
static int cell_is_wide_trail(const WCHAR *line, int k, int *lead);
int is_wide_cp(unsigned int cp);
%(funcs)s

static int failures = 0;
static void ck(const char *n, int got, int want) {
    if (got != want) { printf("[FAIL] %s: got %d want %d\n", n, got, want); failures++; }
    else             { printf("[ok]   %s: %d\n", n, got); }
}

int main(void) {
    /* "a 保 x"：col0 'a', col1 ' ', col2 保主, col3 保次(0), col4 'x' */
    WCHAR l1[16] = { 'a',' ',0x4FDD,0,'x', 0,0,0,0,0,0,0,0,0,0,0 };
    int n = 16;

    /* 向右扩展 anchor=0：指针在保主格 col2 -> 跳到次格 col3（跨过整字） */
    ck("右拖:指针在汉字主格(2)->跳到次格3", copy_quantize_cursor(l1,n,2,0), 3);
    /* 指针已在次格 col3 -> 保持 3（整字已选中） */
    ck("右拖:指针在汉字次格(3)->保持3", copy_quantize_cursor(l1,n,3,0), 3);
    /* 指针在窄字符 col4('x') -> 原样 4 */
    ck("右拖:指针在ASCII x(4)->保持4", copy_quantize_cursor(l1,n,4,0), 4);
    ck("右拖:指针在ASCII a(0)->保持0", copy_quantize_cursor(l1,n,0,0), 0);
    ck("右拖:指针在空格(1)->保持1", copy_quantize_cursor(l1,n,1,0), 1);

    /* 向左扩展 anchor=4：指针在汉字次格 col3 -> 退到主格 col2 */
    ck("左拖:指针在汉字次格(3)->退到主格2", copy_quantize_cursor(l1,n,3,4), 2);
    /* 指针在主格 col2 -> 保持 2（已在字首） */
    ck("左拖:指针在汉字主格(2)->保持2", copy_quantize_cursor(l1,n,2,4), 2);
    ck("左拖:指针在空格(1)->保持1", copy_quantize_cursor(l1,n,1,4), 1);

    /* emoji：😀 U+1F600 高0xD83D(col1) 低0xDE00(col2)，col0 'A' col3 'B' */
    WCHAR l2[16]; memset(l2,0,sizeof l2);
    l2[0]='A'; l2[1]=0xD83D; l2[2]=0xDE00; l2[3]='B';
    ck("右拖:指针在emoji高代理(1)->跳到低代理2", copy_quantize_cursor(l2,n,1,0), 2);
    ck("右拖:指针在emoji低代理(2)->保持2", copy_quantize_cursor(l2,n,2,0), 2);
    ck("左拖:指针在emoji低代理(2)->退到高代理1", copy_quantize_cursor(l2,n,2,3), 1);

    /* 连续两汉字 "中文"：col0 中主 col1 中次 col2 文主 col3 文次 col4 'z' */
    WCHAR l3[8] = { 0x4E2D,0,0x6587,0,'z',0,0,0 };
    ck("右拖:中主(0)->中次1", copy_quantize_cursor(l3,8,0,0), 1);
    ck("右拖:文主(2)->文次3", copy_quantize_cursor(l3,8,2,0), 3);
    ck("左拖:文次(3)->文主2", copy_quantize_cursor(l3,8,3,4), 2);
    ck("左拖:中次(1)->中主0", copy_quantize_cursor(l3,8,1,4), 0);

    if (failures) { printf("\n%d FAILURE(S)\n", failures); return 1; }
    printf("\nALL COPY-QUANTIZE CHECKS PASSED\n");
    return 0;
}
"""


def extract(src, name):
    idx = src.find(name)
    if idx < 0:
        raise SystemExit("找不到 " + name)
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
    funcs = extract(src, "cell_is_wide_trail") + "\n" + extract(src, "copy_quantize_cursor")
    h = HARNESS.replace("%(funcs)s", funcs)
    with tempfile.TemporaryDirectory() as td:
        stub = os.path.join(td, "stub")
        os.makedirs(stub)
        open(os.path.join(stub, "windows.h"), "w").write(
            "#pragma once\n#include <stddef.h>\n#include <wchar.h>\n"
            "typedef unsigned short WORD; typedef wchar_t WCHAR;\n"
            "typedef unsigned long DWORD; typedef void* HANDLE; typedef int BOOL;\n")
        for hdr in ["shellapi.h", "process.h", "windowsx.h"]:
            open(os.path.join(stub, hdr), "w").write('#pragma once\n#include "windows.h"\n')
        uo = os.path.join(td, "utf8.o")
        p = subprocess.run(["gcc", "-O1", "-g", "-fsanitize=address,undefined",
                            "-I", stub, "-I", INC, "-c", os.path.join(SRC, "utf8.c"), "-o", uo],
                           capture_output=True, text=True)
        if p.returncode != 0:
            print(p.stderr); raise SystemExit("utf8 编译失败")
        hc = os.path.join(td, "h.c"); bp = os.path.join(td, "h")
        open(hc, "w").write(h)
        p = subprocess.run(["gcc", "-O1", "-g", "-fsanitize=address,undefined",
                            "-Wall", "-Wextra", "-Werror", "-I", INC, hc, uo, "-o", bp],
                           capture_output=True, text=True)
        if p.returncode != 0:
            print(p.stdout); print(p.stderr); raise SystemExit("编译失败")
        r = subprocess.run([bp], capture_output=True, text=True,
                           env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
        print(r.stdout)
        if r.returncode != 0:
            print(r.stderr); raise SystemExit(1)
    print("[OK] verify_copy_quantize passed")


if __name__ == "__main__":
    main()
