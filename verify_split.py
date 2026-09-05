#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""verify_split.py — 分屏树 / 纯布局回归（v1.8.33）。

分屏树（src/split.c）是纯逻辑（不碰 Win32/g_mux 的布局部分）：叶子 pane 经
split_do 原地变成内部节点（左右 SPLIT_V / 上下 SPLIT_H），split_layout 按外接
矩形递归算出每个叶子 pane 的 PaneRect，中间留 1 行/列边框。

本脚本用 tests/stub 的 windows.h 替身编译【真实 src/split.c】+ 测试驱动，断言：
  * 单叶子 / 切分后叶子数；
  * 左右切分：两 pane 宽之和 + 1 边框 = 外接宽、等高、右 pane 起点 = 左宽+1；
  * 二次（上下）切分：右侧 pane 占满高、左上下高之和 + 1 边框 = 外接高；
  * 邻接导航（重叠优先）：下邻/右邻正确，边界方向返回自身；
  * Tab 视觉次序循环覆盖所有 pane；
  * 关闭一个叶子后树收缩、剩余 pane 布局正确；
  * 拖分隔线（resize）改变对应 frac_pct。
变异：把 SPLIT_V 边框从 1 列改成 0（layout 不留边框），宽度断言立即失败。
"""

import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, "src")
INC = os.path.join(ROOT, "include")
STUB = os.path.join(ROOT, "tests", "stub")

# split.c 的高层操作会引用 g_mux / pane.h（Win32 类型），本测试只覆盖纯布局与
# 节点池（split_reset/split_new_leaf/split_do/split_remove_leaf/split_layout/
# neighbor/next/resize_set_frac 之前的部分）。因此测试驱动直接用节点池 + 纯
# split_layout，不链接 pane.c。
HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "split.h"

static int failures = 0;
static void ck(const char *n, int cond) {
    if (!cond) { printf("[FAIL] %s\n", n); failures++; }
    else       printf("[ok]   %s\n", n);
}

int main(void) {
    split_reset();
    SplitNode *N = split_nodes();

    int r0 = split_new_leaf(0);
    ck("单叶子根", r0 == 0);
    ck("叶子数=1", split_count_leaves(r0) == 1);
    ck("first leaf pane=0", split_first_leaf(r0) == 0);

    int p1 = split_do(r0, SPLIT_V, 1);
    ck("垂直切分原地变内部(根不变)", p1 == r0);
    ck("根变内部节点", N[p1].leaf == 0);
    ck("叶子数=2", split_count_leaves(p1) == 2);
    ck("pane0/pane1 在树中", split_find_leaf(p1,0)>=0 && split_find_leaf(p1,1)>=0);

    PaneRect rects[16];
    memset(rects, 0, sizeof(rects));
    split_layout(p1, 0, 0, 80, 24, N, rects);
    ck("左 pane 原点(0,0)", rects[0].c0==0 && rects[0].r0==0);
    ck("右 pane 起点=左宽+1", rects[1].c0 == rects[0].cols + 1);
    ck("左右宽+边框=80", rects[0].cols + 1 + rects[1].cols == 80);
    ck("左右等高24", rects[0].rows==24 && rects[1].rows==24);
    ck("两 pane valid", rects[0].valid && rects[1].valid);

    int leaf0 = split_find_leaf(p1, 0);
    int p2 = split_do(leaf0, SPLIT_H, 2);
    ck("二次(上下)切分成功", p2 >= 0);
    ck("叶子数=3", split_count_leaves(p1) == 3);
    memset(rects, 0, sizeof(rects));
    split_layout(p1, 0, 0, 80, 24, N, rects);
    ck("3 pane valid", rects[0].valid && rects[1].valid && rects[2].valid);
    ck("右 pane 占满高24", rects[1].rows == 24);
    ck("左上下高+边框=24", rects[0].rows + 1 + rects[2].rows == 24);
    ck("左上左下同宽", rects[0].cols == rects[2].cols);

    ck("pane2(左下) 右邻=pane1(右)", split_neighbor_pane(p1,2,'R')==1);
    ck("pane0(左上) 下邻=pane2(左下)", split_neighbor_pane(p1,0,'D')==2);
    ck("pane1 左邻 在左列", split_neighbor_pane(p1,1,'L')!=1);
    ck("pane0 上邻=自身(边界)", split_neighbor_pane(p1,0,'U')==0);
    ck("pane1 右邻=自身(边界)", split_neighbor_pane(p1,1,'R')==1);

    int o0 = split_first_leaf(p1);
    int o1 = split_next_pane(p1,o0,1);
    int o2 = split_next_pane(p1,o1,1);
    int back = split_next_pane(p1,o2,1);
    ck("Tab 循环回起点", back==o0);
    ck("Tab 覆盖3个不同 pane", o0!=o1 && o1!=o2 && o0!=o2);
    ck("Tab 反向回退", split_next_pane(p1,o1,0)==o0);

    int leaf2 = split_find_leaf(p1, 2);
    int rem=-1;
    int root_after = split_remove_leaf(p1, leaf2, &rem);
    ck("关闭回填 pane=2", rem==2);
    ck("关闭后叶子数=2", split_count_leaves(root_after)==2);
    ck("pane2 已移除", split_find_leaf(root_after,2)<0);
    memset(rects,0,sizeof(rects));
    split_layout(root_after,0,0,80,24,N,rects);
    ck("关闭后 pane0 占满左列高24", rects[0].valid && rects[0].rows==24);

    int before = N[root_after].frac_pct;
    split_resize_pane(root_after, 0, 'R', 5);
    ck("向右扩 a 占比+5", N[root_after].frac_pct == before+5);
    split_resize_pane(root_after, 1, 'L', 5);   /* pane1 在 b，向左扩也增大 a */
    ck("b 向左扩 a 占比再+5", N[root_after].frac_pct == before+10);
    split_resize_set_frac(root_after, 0, 'V', 70);
    ck("set_frac 直接设 70", N[root_after].frac_pct == 70);
    split_resize_set_frac(root_after, 1, 'V', 70);  /* pane1 在 b：a 占比=30 */
    ck("set_frac 从 b 侧设 a=100-70", N[root_after].frac_pct == 30);

    if (failures) { printf("\n%d FAILURE(S)\n", failures); return 1; }
    printf("\nSPLIT TESTS PASSED\n");
    return 0;
}
"""


STUB_EXTRA = r"""
typedef wchar_t WCHAR;
typedef struct { union { WCHAR UnicodeChar; char AsciiChar; } Char; unsigned short Attributes; } CHAR_INFO;
typedef struct { long dummy; } CRITICAL_SECTION;
typedef struct { unsigned long dwEventMask; } MOUSE_EVENT_RECORD;
typedef void *HPCON;
#ifndef FOREGROUND_RED
#define FOREGROUND_RED 4
#define FOREGROUND_GREEN 2
#define FOREGROUND_BLUE 1
#define FOREGROUND_INTENSITY 8
#endif
#ifndef __stdcall
#define __stdcall
#endif
"""


def main() -> int:
    print("=== 分屏树 / 布局验证 (verify_split.py) ===")
    with tempfile.TemporaryDirectory() as td:
        # 组装增强 stub windows.h（tests/stub 内容 + 补类型）。
        stub_win = os.path.join(STUB, "windows.h")
        with open(stub_win, encoding="utf-8") as f:
            stub_txt = f.read()
        if "CHAR_INFO" not in stub_txt:
            stub_txt = stub_txt + "\n" + STUB_EXTRA
        for name in ("shellapi.h", "process.h"):
            sp = os.path.join(STUB, name)
            if os.path.exists(sp):
                with open(sp, encoding="utf-8") as f:
                    content = f.read()
                with open(os.path.join(td, name), "w", encoding="utf-8") as o:
                    o.write(content)
        with open(os.path.join(td, "windows.h"), "w", encoding="utf-8") as f:
            f.write(stub_txt)
        h = os.path.join(td, "h.c")
        exe = os.path.join(td, "h.bin")
        # 驱动需要 g_mux 等全局符号（split 高层函数引用），提供最小定义。
        globs = os.path.join(td, "globs.c")
        with open(globs, "w", encoding="utf-8") as f:
            f.write(
                '#include "common.h"\n#include "types.h"\n'
                "MuxState g_mux;\n"
                "int g_split_zoom_dummy;\n")
        open(h, "w", encoding="utf-8").write(HARNESS)
        cp = subprocess.run(
            ["gcc", "-O1", "-Wall", "-Wextra", "-Werror",
             "-I" + td, "-I" + INC, h, globs, os.path.join(SRC, "split.c"),
             "-o", exe, "-lm"],
            capture_output=True, text=True)
        if cp.returncode != 0:
            print(cp.stderr, file=sys.stderr)
            print("FAIL: split harness 编译失败", file=sys.stderr)
            return 1
        run = subprocess.run([exe], capture_output=True, text=True)
        print(run.stdout)
        if run.returncode != 0:
            print(run.stderr, file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
