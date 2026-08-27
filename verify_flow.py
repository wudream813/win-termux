#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# v8.52 端到端流程模拟：右键 tab -> 菜单 -> [1] 改颜色 -> 选色器 -> hover 8 -> 点击 8
# 严格镜像 termux.cpp 中 handle_mouse 的新控制流（v8.52 重构后）

HOST_COLS, HOST_ROWS = 120, 30
TOP = 2
CTX_W, CP_W = 24, 30

# ---- 状态 ----
class Mux:
    def __init__(self):
        self.chooser_mode = 0
        self.ctx_mode = 0          # 0=off 1=menu 2=color picker
        self.ctx_pane = 0
        self.rename_mode = 0
        self.pop_anchor_x = -1
        self.panes = [{"color": 0} for _ in range(3)]
        self.needs_redraw = 0
        self.events = []

m = Mux()

def popup_left():
    anchor = m.pop_anchor_x if m.pop_anchor_x >= 0 else g_mouse_x
    width = CP_W if m.ctx_mode == 2 else CTX_W
    left = anchor + 1 if anchor >= 0 else 1
    if left + width - 1 > HOST_COLS:
        left = left - width + 1
    return max(1, left)

g_mouse_x, g_mouse_y = -1, -1

# ---- 镜像 handle_mouse 的简化控制流 ----
def handle_mouse(mx, my, btn_state, flags):
    global g_mouse_x, g_mouse_y
    moved = (mx != g_mouse_x or my != g_mouse_y)
    g_mouse_x, g_mouse_y = mx, my
    if moved and (my == 0 or m.chooser_mode or m.ctx_mode or m.rename_mode):
        m.needs_redraw = 1
    if my == 0:
        return "tabbar"
    popup_open = m.chooser_mode or m.ctx_mode
    if popup_open:
        pbtn = btn_state != 0
        if pbtn and flags in (0, 2):   # 0=press 2=double
            if m.ctx_mode:
                left = popup_left()
                r, c = my + 1, mx + 1
                if m.ctx_mode == 1:
                    if r == TOP + 1 and left <= c < left + CTX_W:
                        m.ctx_mode = 2
                        m.needs_redraw = 1
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
                            if 0 <= w < 4:
                                swatch = base + w
                    if 1 <= swatch <= 8:
                        m.panes[m.ctx_pane]["color"] = swatch
                        m.ctx_mode = 0
                        m.needs_redraw = 1
                        return f"picked#{swatch}"
                    m.ctx_mode = 0
                    return "picker-cancel"
            else:
                # chooser
                left = popup_left()
                r, c = my + 1, mx + 1
                in_box = (TOP <= r < TOP + 4) and (left <= c < left + CTX_W)
                if in_box and r == TOP + 1:
                    m.chooser_mode = 0
                    return "new-cmd"
                if in_box and r == TOP + 2:
                    m.chooser_mode = 0
                    return "new-ps"
                m.chooser_mode = 0
                return "chooser-cancel"
        return "swallowed"
    if m.rename_mode:
        return "swallowed-rename"
    return "pane"

# ---- 模拟 hover（镜像 render_color_picker v8.52 公式）----
def hover_swatch(mx0, my0):
    left = popup_left()
    for row in (0, 1):
        y_1b = TOP + 1 + row
        for k in range(4):
            x_1b = left + 2 + 4 * k
            if my0 == y_1b - 1 and x_1b - 1 <= mx0 < x_1b + 3:
                return row * 4 + k + 1
    return None

steps = []
# 1) 右键 tab 0（模拟 tab 命中） -> 菜单打开
m.pop_anchor_x = 40
m.ctx_mode = 1; m.ctx_pane = 0
steps.append(("已设置", m.ctx_mode == 1 and m.ctx_pane == 0))
# 2) 点击菜单 [1] 改颜色（菜单行 r=3, c=40+5）-> 应进入选色器
steps.append((handle_mouse(44, 2, 1, 0) == "menu->picker", "菜单[1] -> 选色器"))
steps.append((m.ctx_mode == 2, "ctx_mode==2"))
# 3) 鼠标移到 8 号色块上方（8 号 = row1,col3）。
# left is ANSI 1-based, so choose the second screen column of its 4-column swatch.
x8 = popup_left() + 14
hover = hover_swatch(x8, 3)
steps.append((hover == 8, f"hover 命中 8（got {hover}）"))
# 4) 点击 8 号色块；mx+1 is ANSI column and maps to dc=12..15.
r = handle_mouse(x8, 3, 1, 0)
steps.append((r == "picked#8", f"点击 8 号 -> picked#8（got {r}）"))
# 5) 验证颜色已写入
steps.append((m.panes[0]["color"] == 8, f"颜色写入 8（got {m.panes[0]['color']}）"))
steps.append((m.ctx_mode == 0, "选色器已关闭"))
# 6) 再次打开菜单，点击空白处取消
m.ctx_mode = 1; m.pop_anchor_x = 40
r = handle_mouse(60, 6, 1, 0)
steps.append((r == "menu-cancel", f"菜单外点击 -> 取消（got {r}）"))
# 7) chooser 独立验证（确认重构没破坏 chooser 点击）
m.chooser_mode = 1; m.pop_anchor_x = 50
r = handle_mouse(51, 2, 1, 0)
steps.append((r == "new-cmd", f"chooser [1] -> new-cmd（got {r}）"))
# 8) 弹窗打开时鼠标移动 -> 必须触发重绘（hover 的前提）
m.ctx_mode = 2; m.pop_anchor_x = 40; m.needs_redraw = 0
handle_mouse(x8, 3, 0, 1)   # flags=MOUSE_MOVED, 无按钮
steps.append((m.needs_redraw == 1, "弹窗内移动触发重绘（hover 更新）"))
# 9) 弹窗打开时按住拖动（MOUSE_MOVED+按钮）不得取消弹窗
m.ctx_mode = 2; m.needs_redraw = 0
r = handle_mouse(55, 3, 1, 1)
steps.append((r == "swallowed" and m.ctx_mode == 2, f"拖动不取消弹窗（got {r}）"))

ok = True
for passed, label in steps:
    mark = "OK " if passed else "FAIL"
    if not passed:
        ok = False
    print(f"  [{mark}] {label}")
print("\n端到端:", "全部通过" if ok else "有失败！")

raise SystemExit(0 if ok else 1)
