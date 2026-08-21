#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# v8.55 验证：修复 color & 7 折叠 bug —— color=8 必须渲染粉色
# 并用用户真实 v8.54 日志回放：右键tab -> 菜单 -> [1]选色 -> 点8号 -> tab 变粉

# 与 termux.cpp 完全一致的调色板
TAB_COLOR_BG = [
    (31,111,235),   # 0 default blue
    (31,111,235),   # 1 blue
    (63,185,80),    # 2 green
    (210,153,34),   # 3 amber
    (137,87,229),   # 4 purple
    (31,136,61),    # 5 teal
    (121,192,255),  # 6 light blue
    (217,119,54),   # 7 orange
    (205,93,173),   # 8 pink
]

def render_tab_color(color):
    """镜像 draw_tab_bar v8.55: clamp 后直接索引"""
    ci = color
    if ci < 0 or ci > 8:
        ci = 0
    return TAB_COLOR_BG[ci]

def old_render_tab_color(color):
    """旧代码 bug: color & 7"""
    return TAB_COLOR_BG[color & 7]

print("=== 1) 每个 color 的渲染颜色（v8.55 修复后）===")
expected = [
    (0, (31,111,235), "默认蓝"),
    (1, (31,111,235), "蓝"),
    (2, (63,185,80), "绿"),
    (3, (210,153,34), "琥珀"),
    (4, (137,87,229), "紫"),
    (5, (31,136,61), "青绿"),
    (6, (121,192,255), "浅蓝"),
    (7, (217,119,54), "橙"),
    (8, (205,93,173), "粉"),
]
ok = True
for c, want, name in expected:
    got = render_tab_color(c)
    good = (got == want)
    ok &= good
    print(f"  [{'OK ' if good else 'FAIL'}] color={c} ({name}) -> RGB{got} (期望 RGB{want})")

print("\n=== 2) 旧代码 bug 演示（color=8 被折叠成 0）===")
for c in (0, 7, 8):
    print(f"  旧: color={c} -> RGB{old_render_tab_color(c)}    新: color={c} -> RGB{render_tab_color(c)}")
bad = old_render_tab_color(8) == (31,111,235) and render_tab_color(8) == (205,93,173)
print(f"  8号 旧=蓝(错) 新=粉(对): {'确认 bug 已修' if bad else '异常'}")

print("\n=== 3) 用户真实日志回放（v8.54 事件序列）===")
# 事件: (mx,my,btn,flags) → 期望动作
# 日志:
#   (13,0) btn=0x2 PRESS  右键 tab1(pane0, cols[8,18)) -> 菜单 ctx=1, ctx_pane=0, anchor=13
#   (16,2) btn=0x1 PRESS  菜单[1]改颜色(r=3=top+1, c=17 in [13,37)) -> 选色器 ctx=2
#   (27,3) btn=0x1 PRESS  8号: r=4=top+2, c=28, dc=28-15=13, which=3, base=5 -> 8
# 之后 color=8 -> tab bar 渲染应为粉(205,93,173)

class Sim:
    def __init__(self):
        self.ctx_mode = 0; self.ctx_pane = -1; self.chooser_mode = 0
        self.anchor = -1; self.color = 0; self.active = 0
        self.tab = [{"pane": -2, "start": 0, "end": 8}, {"pane": 0, "start": 8, "end": 18, "close": (16,17)}, {"pane": -1, "start": 19, "end": 22}]

s = Sim()
TOP, CTX_W = 2, 24

def handle_click(mx, my, btn, flags=0):
    if my == 0:
        # tab bar
        mb = 0
        if btn & 0x1: mb = 0
        elif btn & 0x2: mb = 2
        elif btn & 0x4: mb = 1
        for t in s.tab:
            if not (t["start"] <= mx < t["end"]): continue
            if mb == 2 and t["pane"] >= 0:
                s.ctx_mode = 1; s.ctx_pane = t["pane"]; s.anchor = mx
                return "菜单打开"
        return "tab-其他"
    if s.ctx_mode:
        left = s.anchor if s.anchor >= 0 else mx
        if left + CTX_W > 120: left = (s.anchor if s.anchor >= 0 else mx) - CTX_W
        left = max(0, left)
        r, c = my + 1, mx + 1
        if s.ctx_mode == 1:
            if r == TOP + 1 and left <= c < left + CTX_W:
                s.ctx_mode = 2
                return "进入选色器"
            s.ctx_mode = 0
            return "菜单取消"
        else:
            swatch = -1
            if r in (TOP + 1, TOP + 2):
                base = 1 if r == TOP + 1 else 5
                dc = c - (left + 2)
                if dc >= 0:
                    w = dc // 4
                    if 0 <= w < 4: swatch = base + w
            if 1 <= swatch <= 8:
                s.color = swatch
                s.ctx_mode = 0
                return f"选中{swatch}号"
            s.ctx_mode = 0
            return "选色器取消"
    return "pane"

print("  (13,0) 右键 ->", handle_click(13, 0, 0x2), f"(ctx={s.ctx_mode} pane={s.ctx_pane} anchor={s.anchor})")
print("  (16,2) 左键 ->", handle_click(16, 2, 0x1), f"(ctx={s.ctx_mode})")
r = handle_click(27, 3, 0x1)
print("  (27,3) 左键 ->", r, f"(ctx={s.ctx_mode})")
print("  color =", s.color)
# 渲染用色（核心检查）
rc = render_tab_color(s.color)
print("  tab 渲染色 ->", rc, "(应为粉 205,93,173)")
ok &= (s.color == 8 and rc == (205,93,173))

print("\n=== 4) Ctrl+B t 轮换到 8 后渲染色 ===")
# 轮换: 从0 -> 1 -> ... -> 8
c = 0
for _ in range(8):
    c += 1
    if c > 8: c = 1
print(f"  轮换8次后 color={c} 渲染={render_tab_color(c)} (应粉)")
ok &= (render_tab_color(c) == (205,93,173))

print("\n结果:", "全部通过" if ok else "有失败！")

raise SystemExit(0 if ok else 1)
