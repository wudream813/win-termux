#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Invariants for copy mode's selection shapes and the Shift/Alt click session.

v1.8.5 added three things that are easy to break silently:
  * Shift/Alt reshape the selection (text flow vs rectangular block);
  * Shift/Alt + two clicks jump straight into a one-shot copy session where
    Ctrl+C / Enter copy and close, Esc closes, and any other key closes AND is
    still delivered to the pane;
  * the clipboard writer and the renderer must agree on what a block is.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
INPUT = (ROOT / "src" / "input.c").read_text(encoding="utf-8")
RENDER = (ROOT / "src" / "render.c").read_text(encoding="utf-8")
TYPES = (ROOT / "include" / "types.h").read_text(encoding="utf-8")
INPUT_H = (ROOT / "include" / "input.h").read_text(encoding="utf-8")

errors = []


def check(condition, message):
    if not condition:
        errors.append(message)


def func(text, signature):
    start = text.index(signature)
    depth = 0
    i = text.index("{", start)
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[start:j + 1]
    raise AssertionError(signature)


key = func(INPUT, "int handle_copy_mode_key(")
mouse = func(INPUT, "void handle_mouse(")
clip = func(INPUT, "void copy_selection_to_clipboard(")

# --- state -----------------------------------------------------------------
check("extern int g_copy_block;" in TYPES and "extern int g_copy_quick;" in TYPES,
      "复制模式没有独立的块选 / 快捷会话状态")
check("int handle_copy_mode_key(KEY_EVENT_RECORD *ke);" in INPUT_H,
      "handle_copy_mode_key 没有返回“是否需要把按键转发给终端”")
check("if (!handle_copy_mode_key(ke)) return;" in INPUT,
      "handle_key 没有在快捷复制会话结束后继续投递那个按键")

# --- keyboard --------------------------------------------------------------
check("vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU" in key,
      "修饰键本身会被当成“其它键”而立刻退出快捷会话")
check(key.index("vk == VK_SHIFT || vk == VK_CONTROL") < key.index("if (g_copy_quick)"),
      "修饰键过滤必须排在“其它键退出”之前")
check("int is_ctrl_c = (has_ctrl && (vk == 'C' || uc == 3));" in key and
      "if (is_ctrl_c || vk == VK_RETURN)" in key,
      "Ctrl+C / Enter 没有统一为“复制并关闭”")
check(key.index("if (vk == VK_ESCAPE)") < key.index("if (g_copy_quick)") and
      key.index("if (is_ctrl_c || vk == VK_RETURN)") < key.index("if (g_copy_quick)"),
      "Esc / Ctrl+C / Enter 必须先于“其它键退出并发送”被处理")
check("if (is_motion && (has_shift || has_alt))" in key and
      "g_copy_block = has_alt ? 1 : 0;" in key and
      "if (!g_copy_sel_active) copy_mode_anchor_here(p, s);" in key,
      "Shift/Alt + 方向键没有改变选区形状（Shift=行选, Alt=块选）")
check("copy_mode_leave(p);\n        return 1;" in key,
      "快捷会话里的其它键没有“退出并把按键交回终端”")
check("copy_mode_yank(p, s);" in key and "g_copy_block" in func(INPUT, "static void copy_mode_yank("),
      "复制时没有按当前选区形状取内容")

# --- mouse -----------------------------------------------------------------
check("quick_shift" in mouse and "quick_alt" in mouse and
      "g_copy_quick = 1;" in mouse and
      "g_copy_block = quick_alt ? 1 : 0;" in mouse,
      "Shift/Alt 点击没有直接进入（块）复制模式")
check("/* Second corner: only the end point moves. */" in mouse,
      "第二次点击没有只移动选区终点")
check(mouse.index("quick_shift") < mouse.index("if (!s->mouse_tracking && !p->is_settings"),
      "Shift/Alt 点选必须排在普通拖拽选择之前")
check(mouse.index("quick_shift") < mouse.index("if (s->mouse_tracking == 0) {"),
      "Shift/Alt 点选必须在鼠标事件转发给全屏程序之前生效")
check("me->dwEventFlags == MOUSE_MOVED && g_copy_mode && g_copy_quick" in mouse,
      "按住 Shift/Alt 拖动时选区终点不会跟随")

# --- clipboard / renderer agreement ----------------------------------------
check("int block_x0 = sx < ex ? sx : ex;" in clip and
      "int x_start = block ? block_x0" in clip and
      "int x_end = block ? block_x1" in clip,
      "块复制没有对每一行取同一段列")
# v1.8.26：块（矩形）复制不再逐行裁掉所有尾随空格，而是「只有需要向右补全
# 的行才补到块右边界；完全空的行保留为空行」（可复制出「啊/空行/c」这种）。
check("if (valid_x1 < x_start)" in clip and "补全到块右边界" in clip and
      "wlen = row_wlen_start;" in clip,
      "块复制没有按『空行留空、内容行补全到块右边界』处理")
check("void copy_range_to_clipboard(Pane *p, int sx, int sy_abs, int ex, int ey_abs) {\n"
      "    copy_selection_to_clipboard(p, sx, sy_abs, ex, ey_abs, 0, 0);" in INPUT,
      "拖拽选择没有复用同一个复制实现（闭合区间）")

# v1.8.27：键盘选区为半开区间（默认不选任何格、→ 一次只选中跨过的那个字符）。
check("int halfopen = g_copy_quick ? 0 : 1;" in INPUT and
      "int half = !g_copy_quick;" in RENDER,
      "键盘选区没有按半开区间（caret）处理")
check("sel_max_x -= 1;            /* 右端 caret 排他 */" in RENDER or
      "sel_max_x -= 1;" in RENDER,
      "半开选区右端 caret 没有排他（默认会多选中一格）")
check("copy_selection_to_clipboard(p, g_copy_anchor_x, g_copy_anchor_abs_y," in INPUT
      and "halfopen)" in INPUT,
      "yank 没有把半开标志传给复制实现")
check("int sel_active = 0, sel_block = 0" in RENDER and
      "if (g_copy_block) {" in RENDER and
      "in_sel = (cur_cell_abs_y >= sel_min_abs_y && cur_cell_abs_y <= sel_max_abs_y &&" in RENDER,
      "渲染没有画出矩形选区（会和复制到剪贴板的内容不一致）")
check('g_copy_block ? "块选区" : "行选区"' in RENDER,
      "复制模式状态条没有区分行选 / 块选")

if errors:
    print("复制模式验证失败:", file=sys.stderr)
    for e in errors:
        print("  FAIL:", e, file=sys.stderr)
    raise SystemExit(1)

print("复制模式验证通过：Shift/Alt 改变选区形状、点选会话按键语义、块选渲染与复制一致。")
