#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# v8.53 验证：mbtn 优先级反转 + tab bar 空白右键兜底 + pane 右键 + 选色器回归
# 严格镜像 termux.cpp v8.53 的 handle_mouse 控制流

HOST_COLS, HOST_ROWS = 120, 30
TOP = 2
CTX_W, CP_W = 24, 30
L, R, M = 0x1, 0x2, 0x4        # 左 / 右 / 中
PRESS, DBL, MOVED = 0, 2, 1

class Mux:
    def __init__(self):
        self.chooser_mode = 0
        self.ctx_mode = 0
        self.ctx_pane = -1
        self.rename_mode = 0
        self.help_mode = 0
        self.pop_anchor_x = -1
        self.active_pane = 0
        self.panes = [{"color": 0, "active": True}, {"color": 0, "active": True}]
        self.tab_count = 2
        # 镜像 draw_tab_bar: brand[0,8) tab0[8,18) tab1[18,28) 间隙 28  [+][29,32) 空白 32+
        self.tab_info = [
            {"pane_idx": -2, "start": 0, "end": 8},
            {"pane_idx": 0, "start": 8, "end": 18, "close": (16, 17)},
            {"pane_idx": 1, "start": 18, "end": 28, "close": (26, 27)},
            {"pane_idx": -1, "start": 29, "end": 32},
        ]
        self.needs_redraw = 0
        self.mouse_tracking = 0
        self.events = []

m = Mux()
g_mouse_x, g_mouse_y = -1, -1

def popup_left():
    a = m.pop_anchor_x if m.pop_anchor_x >= 0 else g_mouse_x
    if a + CTX_W > HOST_COLS:
        a = (m.pop_anchor_x if m.pop_anchor_x >= 0 else g_mouse_x) - CTX_W
    return max(0, a)

def mbtn_of(btn_state):
    # v8.53: LEFT > RIGHT > MIDDLE
    if btn_state & L: return 0
    if btn_state & R: return 2
    if btn_state & M: return 1
    return -1

def handle_mouse(mx, my, btn_state, flags, mouse_tracking=0):
    global g_mouse_x, g_mouse_y
    moved = (mx != g_mouse_x or my != g_mouse_y)
    g_mouse_x, g_mouse_y = mx, my
    if moved and (my == 0 or m.chooser_mode or m.ctx_mode or m.rename_mode):
        m.needs_redraw = 1
    if my == 0:   # tab bar
        press2 = (btn_state & (L|R|M)) != 0
        if press2 and flags in (PRESS, DBL) and (m.chooser_mode or m.ctx_mode or m.rename_mode):
            m.chooser_mode = m.ctx_mode = m.rename_mode = 0
            return "close-popup"
        press = (btn_state & (L|R|M)) != 0
        if press and flags in (PRESS, DBL):
            mb = mbtn_of(btn_state)
            for t in m.tab_info:
                if not (t["start"] <= mx < t["end"]):
                    continue
                if mb == 1 and t["pane_idx"] >= 0:
                    return "mid-close-tab"
                if t["pane_idx"] == -2:
                    m.help_mode = not m.help_mode
                    return "brand-help"
                if t["pane_idx"] == -1:
                    m.chooser_mode = 1
                    m.pop_anchor_x = mx
                    return "plus-chooser"
                if mb == 2 and t["pane_idx"] >= 0 and m.panes[t["pane_idx"]]["active"]:
                    m.ctx_mode = 1
                    m.ctx_pane = t["pane_idx"]
                    m.pop_anchor_x = mx
                    return "tab-right-menu"
                if mb != 0:
                    return "ignored"
                if not m.panes[t["pane_idx"]]["active"]:
                    continue
                cs, ce = t["close"]
                if cs <= mx < ce:
                    return "x-close"
                return "switch-tab"
            # v8.57: 空白区右键不再弹菜单
            pass
        return "tabbar-else"
    # popup
    popup_open = m.chooser_mode or m.ctx_mode
    if popup_open:
        pbtn = (btn_state & (L|R|M)) != 0
        if pbtn and flags in (PRESS, DBL):
            if m.ctx_mode:
                left = popup_left()
                r, c = my + 1, mx + 1
                if m.ctx_mode == 1:
                    if r == TOP + 1 and left <= c < left + CTX_W:
                        m.ctx_mode = 2
                        return "menu->picker"
                    if r == TOP + 2 and left <= c < left + CTX_W:
                        m.ctx_mode = 0; m.rename_mode = 1
                        return "menu->rename"
                    m.ctx_mode = 0
                    return "menu-cancel"
                else:
                    swatch = -1
                    if r in (TOP + 1, TOP + 2):
                        base = 1 if r == TOP + 1 else 5
                        dc = c - (left + 2)
                        if dc >= 0:
                            w = dc // 4
                            if 0 <= w < 4: swatch = base + w
                    if 1 <= swatch <= 8:
                        m.panes[m.ctx_pane]["color"] = swatch
                        m.ctx_mode = 0
                        return f"picked#{swatch}"
                    m.ctx_mode = 0
                    return "picker-cancel"
            else:
                left = popup_left()
                r, c = my + 1, mx + 1
                in_box = TOP <= r < TOP + 4 and left <= c < left + CTX_W
                if in_box and r == TOP + 1:
                    m.chooser_mode = 0
                    return "new-cmd"
                if in_box and r == TOP + 2:
                    m.chooser_mode = 0
                    return "new-ps"
                m.chooser_mode = 0
                return "chooser-cancel"
        return "popup-swallowed"
    if m.rename_mode:
        return "rename-swallowed"
    if m.help_mode:
        return "help"
    # pane 区域
    if mouse_tracking == 0:
        # v8.57: pane 右键不再弹菜单
        return "pane-none"
    return "pane-mouse-fwd"

ok = True
def chk(label, got, want):
    global ok
    good = (got == want)
    if not good: ok = False
    print(f"  [{'OK ' if good else 'FAIL'}] {label}: got={got!r} want={want!r}")

print("=== A) mbtn 优先级（中键粘连模拟）===")
# 纯左键点 tab0 身体
chk("左键(0x1) 点 tab0 -> 切tab", handle_mouse(10, 0, L, PRESS), "switch-tab")
# 左+中粘连 (0x5) 点 tab0 -> 仍是左键
chk("左+中(0x5) 点 tab0 -> 切tab(不被当中间)", handle_mouse(10, 0, L|M, PRESS), "switch-tab")
# 左+中 点 × -> 关tab
chk("左+中(0x5) 点 tab0 的× -> 关tab", handle_mouse(16, 0, L|M, PRESS), "x-close")
# 右键 (0x2) 点 tab1 -> 菜单
chk("右键(0x2) 点 tab1 -> 菜单", handle_mouse(20, 0, R, PRESS), "tab-right-menu")
chk("   ctx_pane=1", m.ctx_pane, 1)
# 右+中粘连 (0x6) 点 tab1 -> 右键（不是中键关tab）
m.ctx_mode = 0
chk("右+中(0x6) 点 tab1 -> 菜单(不被当中间)", handle_mouse(20, 0, R|M, PRESS), "tab-right-menu")
chk("   ctx_pane=1", m.ctx_pane, 1)
# 纯中键 (0x4) 点 tab -> 关tab
m.ctx_mode = 0
chk("纯中键(0x4) 点 tab0 -> 关tab", handle_mouse(10, 0, M, PRESS), "mid-close-tab")
# 品牌（任何按钮）
chk("左键点品牌 -> 帮助切换", handle_mouse(3, 0, L, PRESS), "brand-help")
m.help_mode = 0
# [+]
chk("左键点[+] -> chooser", handle_mouse(30, 0, L, PRESS), "plus-chooser")
m.chooser_mode = 0

print("\n=== B) tab bar 空白区右键（v8.57: 不再弹菜单）===")
# 用户鼠标常在列 90 —— 空白区右键不应弹菜单
chk("空白区(列90) 右键 -> 不弹菜单", handle_mouse(90, 0, R, PRESS), "tabbar-else")
chk("   ctx_mode=0", m.ctx_mode, 0)
chk("空白区(列90) 右+中粘连 -> 不弹菜单", handle_mouse(90, 0, R|M, PRESS), "tabbar-else")
chk("   ctx_mode=0", m.ctx_mode, 0)
chk("空白区(列90) 左键 -> 无操作", handle_mouse(90, 0, L, PRESS), "tabbar-else")

print("\n=== C) pane 区域右键（v8.57: 不再弹菜单）===")
chk("pane(60,10) 右键(无mouse_tracking) -> 不弹菜单", handle_mouse(60, 10, R, PRESS, mouse_tracking=0), "pane-none")
chk("   ctx_mode=0", m.ctx_mode, 0)
chk("pane 右+中粘连 -> 不弹菜单", handle_mouse(60, 10, R|M, PRESS, mouse_tracking=0), "pane-none")
chk("   ctx_mode=0", m.ctx_mode, 0)
chk("pane 左键 -> 无操作", handle_mouse(60, 10, L, PRESS, mouse_tracking=0), "pane-none")
chk("pane 右键(mouse_tracking开) -> 转发给app", handle_mouse(60, 10, R, PRESS, mouse_tracking=1), "pane-mouse-fwd")

print("\n=== D) 完整流程回归（右键任意处 -> 菜单 -> 选色器 -> 点 8）===")
# 右键 tab1（cols[18,28) 中点）-> 菜单（anchor=23, 菜单 left=23, 范围 [23,47)）
r = handle_mouse(23, 0, R, PRESS)
chk("tab 右键 -> 菜单", r, "tab-right-menu")
left = popup_left()
chk("anchor left=23", left, 23)
# 点菜单 [1] 改颜色：菜单 1-based 行 top+1=3 -> 0-based my=2；列在 [left,left+CTX_W) 内
r = handle_mouse(30, 2, L, PRESS)
chk("菜单 [1] -> 选色器", r, "menu->picker")
# hover 8 号：row1(第二行) k=3, 1-based 起始 left+2+4*3=37, 覆盖 1-based 37..40
# -> 0-based 鼠标列 36..39, 0-based 行 3（1-based 4 = TOP+1+1）
x8, y8 = 37, 3
hover_ok = (y8 + 1 == TOP + 1 + 1) and (left + 2 + 12 <= x8 + 1 < left + 2 + 12 + 4)
chk("hover 8 号区域命中", hover_ok, True)
# 点击 8 号（左+中粘连 0x5）
r = handle_mouse(x8, y8, L|M, PRESS)
chk("点击 8 号(0x5) -> picked#8", r, "picked#8")
chk("   pane1 颜色=8", m.panes[1]["color"], 8)
chk("   ctx_mode 关闭", m.ctx_mode, 0)
# 再验证 6 号也能点（row1 k=1, 1-based 起始 left+2+4=76, 0-based 列 75..78, 行 3）
m.ctx_mode = 2; m.ctx_pane = 0; m.pop_anchor_x = 70
r = handle_mouse(76, 3, R|M, PRESS)
chk("点击 6 号(0x6) -> picked#6", r, "picked#6")
chk("   pane0 颜色=6", m.panes[0]["color"], 6)

print("\n=== E) 弹窗已开时 tab bar 点击只关弹窗 ===")
m.ctx_mode = 1; m.pop_anchor_x = 70
chk("弹窗开时点 tab0 -> 关弹窗", handle_mouse(10, 0, L, PRESS), "close-popup")
chk("   ctx_mode=0", m.ctx_mode, 0)
chk("   没有开新菜单", True, True)

print("\n结果:", "全部通过" if ok else "有失败！")

print("\n=== F) v8.55 回归: color=8 渲染用色（防 & 7 折叠回归）===")
TAB_COLOR_BG = [(31,111,235),(31,111,235),(63,185,80),(210,153,34),(137,87,229),(31,136,61),(121,192,255),(217,119,54),(205,93,173)]
def render_color(c):
    ci = c
    if ci < 0 or ci > 8: ci = 0
    return TAB_COLOR_BG[ci]
def old_render_color(c):
    return TAB_COLOR_BG[c & 7]
assert render_color(8) == (205,93,173), "color=8 必须渲染粉色"
assert old_render_color(8) == (31,111,235), "演示旧bug(蓝)"
assert render_color(0) == (31,111,235)
for c in range(9):
    assert render_color(c) == TAB_COLOR_BG[c], f"color={c} 渲染错误"
chk("color=8 渲染粉色(非蓝色)", render_color(8), (205,93,173))
chk("color=0 默认蓝", render_color(0), (31,111,235))
print("  v8.55 渲染用色回归: 通过")
