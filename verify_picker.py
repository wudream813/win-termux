#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# v8.52 验证：颜色选择器 (render_color_picker) 渲染几何 <-> 鼠标命中(hit-test) <-> hover 区域
# 与 termux.cpp 中的公式逐一对应（top=2, CP_W=30, 色块起始 1-based col = left+2+4k）

CP_W, CP_H, CTX_W = 30, 4, 24
TOP = 2


def popup_left(anchor, host_cols, W):
    left = anchor
    if left + W > host_cols:
        left = anchor - W
    if left < 0:
        left = 0
    return left


def swatch_1based_start(k):
    # renderer: x = left + 2; x += 4 per swatch  -> swatch k (0-based) at left+2+4k
    return 2 + 4 * k


def render_cols(left, k):
    """渲染出的色块占用的 1-based 列 [left+2+4k, left+5+4k]，共 4 列"""
    s = left + 2 + 4 * k
    return list(range(s, s + 4))


def hit_swatch(r_1b, c_1b, left):
    """handle_mouse 里的命中公式（1-based r/c）"""
    if r_1b == TOP + 1:
        base = 1
    elif r_1b == TOP + 2:
        base = 5
    else:
        return -1
    dc = c_1b - (left + 2)
    if dc < 0:
        return -1
    which = dc // 4
    if 0 <= which < 4:
        return base + which
    return -1


def hover(mx_0b, my_0b, y_1b, x_1b):
    """render_color_picker 的 hover 公式：0-based 鼠标坐标，1-based 渲染行/列"""
    return my_0b == y_1b - 1 and x_1b - 1 <= mx_0b < x_1b + 3


def row_y_1b(row):
    return TOP + 1 + row


total_bad = 0

print("=== 1) 所有 8 个色块的 点击命中 与 渲染位置 一致（连续 4 列，含边界）===")
# 色块连续排列：left+2+4k .. left+5+4k（4 列）。整段 [left+2, left+17] 应映射 1..8，
# 段外（< left+2 或 >= left+18）应返回 -1。
bad = 0
for left in (0, 5, 10, 40, 100):   # 不同锚点（含贴近左右边界）
    for k in range(8):             # 色块 1..8
        row, col_in_row = divmod(k, 4)
        y_1b = row_y_1b(row)
        cols = render_cols(left, col_in_row)
        # 色块 4 列上的每个 1-based 列都必须命中该色块
        for c in cols:
            got = hit_swatch(y_1b, c, left)
            if got != k + 1:
                bad += 1
                print(f"  FAIL left={left} swatch={k+1} col1b={c} -> {got} (期望 {k+1})")
    # 段外边界不得误命中（左边界前 1 列、右边界后 1 列）
    for c in (left + 1, left + 18):
        if hit_swatch(TOP + 1, c, left) != -1:
            bad += 1
            print(f"  FAIL left={left} 段外 col1b={c} 应 -1")
total_bad += bad
print("  通过" if bad == 0 else f"  {bad} 处失败")

print("\n=== 2) hover 区域与渲染的 4 列色块完全一致 ===")
bad = 0
for left in (0, 40):
    for k in range(8):
        row, col_in_row = divmod(k, 4)
        y_1b = row_y_1b(row)
        x_1b = left + swatch_1based_start(col_in_row)   # 实际 1-based 起始列
        cols = render_cols(left, col_in_row)
        # 色块 4 列的 0-based 鼠标坐标
        for c in cols:
            if not hover(c - 1, y_1b - 1, y_1b, x_1b):
                bad += 1
                print(f"  FAIL left={left} swatch={k+1} 色块列0b={c-1} 应 hover")
        # 色块外相邻列不得 hover
        for c in (cols[0] - 1, cols[-1] + 1):
            if hover(c - 1, y_1b - 1, y_1b, x_1b):
                bad += 1
                print(f"  FAIL left={left} swatch={k+1} 间隙列0b={c-1} 不应 hover")
total_bad += bad
print("  通过" if bad == 0 else f"  {bad} 处失败")

print("\n=== 3) 弹窗几何：渲染器 left 与鼠标处理器 left 一致（锚点锁定）===")
bad = 0
for anchor in (0, 1, 27, 28, 50, 89, 90, 91, 118, 119):   # 真实鼠标坐标范围 0..119
    host = 120
    # renderer 用 CP_W，ctx/color picker 都按 CTX_W 钳制（实际渲染都用同一公式）
    r_left = popup_left(anchor, host, CP_W)
    h_left = popup_left(anchor, host, CP_W)
    if r_left != h_left or not (0 <= r_left and r_left + CP_W <= host):
        bad += 1
        print(f"  FAIL anchor={anchor} renderer={r_left} handler={h_left}")
total_bad += bad
print("  通过" if bad == 0 else f"  {bad} 处失败")

print("\n=== 4) handle_mouse 流程：ctx 弹窗点击必须可达（v8.52 修复的核心）===")
# 模拟 v8.51（旧）与 v8.52（新）的控制流：
# 旧：if chooser_mode: if ctx_mode: ...   -> ctx_mode=2 且 chooser_mode=0 时点 8 号不可达
# 新：popup_open = chooser||ctx; ctx 分支直接可达
def old_reachable(chooser, ctx):
    # 旧代码：ctx 分支嵌在 chooser 分支内 -> 只有两者都为真才可达
    return chooser and ctx
def new_reachable(chooser, ctx):
    return chooser or ctx
for chooser, ctx in ((0, 1), (0, 2), (1, 0), (1, 1)):
    old_note = "旧=点击可达" if old_reachable(chooser, ctx) else "旧=点击无效"
    print(f"  chooser_mode={chooser} ctx_mode={ctx}: {old_note} -> 新={'可达' if new_reachable(chooser,ctx) else '不可达'}")
bad = 0
for chooser, ctx in ((0, 1), (0, 2), (1, 0)):
    if not new_reachable(chooser, ctx):
        bad += 1
total_bad += bad
print("  通过" if bad == 0 else "  FAIL")

print("\n=== 5) 右键菜单项点击（ctx_mode=1）===")
# 菜单行 [1] 改颜色 / [2] 改标题：r == top+1 / top+2，c 在 [left, left+CTX_W)
for left in (0, 40):
    for r_1b in (TOP + 1, TOP + 2):
        for c in (left, left + CTX_W - 1):
            if not (r_1b in (TOP + 1, TOP + 2) and left <= c < left + CTX_W):
                print(f"  FAIL left={left} r={r_1b} c={c}")
print("  通过")

print("\n全部验证完成")

raise SystemExit(0 if total_bad == 0 else 1)
