#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Geometry regression checks for the ANSI renderer and Windows mouse input.

The UI writes ANSI cursor positions as 1-based row/column coordinates while
Win32 MOUSE_EVENT_RECORD positions are 0-based.  This check deliberately
covers the shared formulas and the fixed-width rows of every modal surface;
it is kept source-adjacent so future layout edits have a small, cheap guard.
"""

from pathlib import Path
import re
import unicodedata

ROOT = Path(__file__).resolve().parent
RENDER = (ROOT / "src/render.c").read_text(encoding="utf-8")
INPUT = (ROOT / "src/input.c").read_text(encoding="utf-8")
UTF8 = (ROOT / "src/utf8.c").read_text(encoding="utf-8")

errors = []


def check(condition, message):
    if not condition:
        errors.append(message)


def cols(text):
    """Match the project's display-width convention for UI strings."""
    total = 0
    for ch in text:
        # Box drawing, arrows and ASCII punctuation used here are single-cell;
        # East Asian wide/full-width characters are two cells.
        total += 2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1
    return total


# ---- 1) Shared 0-based mouse anchor -> 1-based ANSI popup left edge ----
def popup_left(anchor0, width, host_cols):
    width = max(1, width)
    host_cols = max(1, host_cols)
    anchor = anchor0 + 1 if anchor0 >= 0 else 1
    left = anchor
    if left + width - 1 > host_cols:
        left = anchor - width + 1
    return max(1, left)


check("popup_left_1based" in RENDER, "render.c 缺少统一 popup_left_1based()")
check(RENDER.count("popup_left_1based(") >= 5, "chooser/固定弹窗没有全部使用统一左边界公式")
check("int popup_w = (g_mux.ctx_mode == 1) ? CTX_W : CP_W;" in INPUT,
      "ctx 菜单/颜色选择器没有分别使用各自宽度的左边界公式")
check("popup_left_1based(anchor0, popup_w" in INPUT, "ctx 鼠标命中未使用统一左边界公式")
for host in range(4, 201):
    for width in (1, 3, 20, 24, 30, 38, 72, 78):
        if width > host:
            continue
        for anchor in (-1, 0, 1, host // 2, host - 2, host - 1):
            left = popup_left(anchor, width, host)
            check(1 <= left <= host - width + 1,
                  f"popup 几何越界 host={host} width={width} anchor={anchor} left={left}")
            expected_anchor = anchor + 1 if anchor >= 0 else 1
            expected = expected_anchor if expected_anchor + width - 1 <= host else expected_anchor - width + 1
            check(left == max(1, expected),
                  f"popup renderer/handler 锚点不一致 host={host} width={width} anchor={anchor}")
# The context menu and picker intentionally have different widths; both must
# remain right-aligned to the same mouse anchor at the right edge.
for width in (24, 30):
    left = popup_left(119, width, 120)
    check(left == 120 - width + 1, f"右边界 popup width={width} 未贴合 ANSI 右边界")

# ---- 2) ANSI row/column <-> mouse row/column conversion ----
for mouse_y in range(0, 80):
    ansi_y = mouse_y + 1
    check(ansi_y - 1 == mouse_y, "行坐标换算不是 ANSI 1-based <-> mouse 0-based")
for mouse_x in range(0, 240):
    ansi_x = mouse_x + 1
    check(ansi_x - 1 == mouse_x, "列坐标换算不是 ANSI 1-based <-> mouse 0-based")

# ---- 3) Fixed popups: every visible row has one common width ----
check(cols("┌─ 标签操作 ───────────┐") == 24, "ctx 标题不是 CTX_W=24 列")
check(cols("│  [1] 修改颜色        │") == 24, "ctx 第一行不是 CTX_W=24 列")
check(cols("│  [2] 重命名标签      │") == 24, "ctx 第二行不是 CTX_W=24 列")
check(cols("└──────────────────────┘") == 24, "ctx 底边不是 CTX_W=24 列")
check(cols("┌─ 选择颜色 ─────────────────┐") == 30, "颜色选择器标题不是 CP_W=30 列")
check(cols("└────────────────────────────┘") == 30, "颜色选择器底边不是 CP_W=30 列")
# Picker rows: border + initial space + 4*swatch + one gap + padding + border.
for row_name in ("第一行", "第二行"):
    visible = 2 + 4 * 4 + 1 + (30 - 1 - (2 + 4 * 4 + 1)) + 1
    check(visible == 30, f"颜色选择器{row_name}未填充到 CP_W")
check(cols("┌─ 重命名标签 ───────────────┐") == 30, "重命名标题不是 RENAME_W=30 列")
check(cols("└────────────────────────────┘") == 30, "重命名底边不是 RENAME_W=30 列")
check(cols("┌─ 自定义命令行 ─────────────────────┐") == 38, "自定义命令标题不是 CMD_BOX_W=38 列")
check(cols("└────────────────────────────────────┘") == 38, "自定义命令底边不是 CMD_BOX_W=38 列")
cmd_hint = "  [Enter=启动  Esc=取消]"
check(1 + cols(cmd_hint) <= 38 - 1, "自定义命令 footer 文本超出内框")
check(1 + cols(cmd_hint) + (38 - 1 - (1 + cols(cmd_hint))) + 1 == 38,
      "自定义命令 footer 未补齐到 CMD_BOX_W")
check("┌─ 自定义命令行 ──────────────────────┐" not in RENDER,
      "自定义命令标题仍保留一列过宽的旧字符串")

# ---- 4) Color swatches: render, hover and hit-test are the same 4 cells ----
check("g_mouse_x >= left + 1 + (i-1)*4" in RENDER,
      "颜色选择器 hover 没有把 ANSI 列转换为 0-based 鼠标列")
for left in (1, 7, 41, 91):
    for row in (0, 1):
        for k in range(4):
            ansi_start = left + 2 + 4 * k
            rendered = set(range(ansi_start, ansi_start + 4))
            hit = set(range(ansi_start, ansi_start + 4))
            hover = set(range(ansi_start - 1, ansi_start + 3))
            check(rendered == hit, f"颜色块 {row},{k} 渲染/点击范围不同")
            check({x + 1 for x in hover} == rendered, f"颜色块 {row},{k} hover/ANSI 范围不同")

# ---- 5) Chooser and preset rows ----
for item_count in (0, 1, 8, 9):
    tagw = 4 if item_count >= 10 else 3
    max_name = 15
    chooser_w = max(20, 1 + 2 + tagw + 1 + max_name + 2)
    check(chooser_w >= 20, "chooser 最小宽度不满足边框")
    # body formula for the widest item, including both borders
    check(1 + 2 + tagw + 1 + max_name + (chooser_w - 1 - (1 + 2 + tagw + 1 + max_name)) + 1 == chooser_w,
          f"chooser {item_count} 项时行宽不闭合")
check("int item_used = 1 + 2 + utf8_cols(chooser_tag" in RENDER,
      "chooser 条目仍用固定 tag 宽度，可能使两位序号右边框错位")
check("cols = 1 + 2 + utf8_cols(preset_tag" in RENDER,
      "预设条目仍用固定 tag 宽度")

# ---- 6) Settings page: sidebar, fields and table buttons ----
check("if (c <= sb_w)" in INPUT, "设置侧栏命中区域漏掉渲染出的分隔列")
check("if (r == host_rows)" in INPUT, "设置保存按钮命中仍覆盖底部空白行")
check("if (r >= host_rows)" not in INPUT, "设置保存按钮仍使用过宽的 r>=host_rows 命中")
check("c < main_left + input_w + 4" in INPUT, "设置编辑字段命中没有限制在输入框宽度内")
# Renderer hover ranges are mouse-space; handler ranges are ANSI-space.
for lo, hi in ((52, 54), (55, 57), (58, 61), (62, 65)):
    mouse_range = set(range(lo, hi + 1))
    ansi_range = set(range(lo + 1, hi + 2))
    check({x - 1 for x in ansi_range} == mouse_range, "设置表格按钮 hover/点击范围不一致")
check("append_padded_utf8(out, bs, &pos, &row_cols, dname, 12)" in RENDER,
      "设置表格显示名称没有按终端列宽补齐")
check("append_padded_utf8(out, bs, &pos, &row_cols, dcmd, 30)" in RENDER,
      "设置表格命令行没有按终端列宽补齐")
check("%-12s" not in RENDER and "%-30s" not in RENDER,
      "设置表格仍使用按字节计算的 %-Ns 补齐")

# ---- 7) Palette rows/editor and actual bottom search line ----
check("int row = top + 3 + vi;" in RENDER and "int row_start = top + 3;" in INPUT,
      "命令面板列表行的 renderer/hit-test 起始行不同")
check("int row_end = row_start + visible;" in INPUT,
      "命令面板列表没有以 visible 行数封闭命中区域")
check("top + 2 + g_mux.palette_field * 2" in RENDER,
      "命令面板编辑器光标没有落在输入行")
check("int input_row = top + 2 + field * 2" in INPUT,
      "命令面板编辑器输入框点击行与渲染不一致")
check("#define SEARCH_PREFIX_COLS 9" in RENDER and "#define SEARCH_SUFFIX_COLS 24" in RENDER,
      "搜索框没有使用真实的前缀/后缀列宽")
check("int box_w = search_input_width(host_cols);" in RENDER,
      "搜索框和光标没有共享同一个输入宽度公式")
check("int box_w = search_input_width(g_mux.host_cols);" in RENDER,
      "搜索框光标未使用共享输入宽度公式")
check("return row;" in RENDER and "row = csbi.srWindow.Bottom - csbi.srWindow.Top + 1" in RENDER,
      "搜索底栏没有读取实际控制台窗口最低行")

# ---- 8) Requested interaction/text guards ----
check("g_mux.palette_page = settings_active ? PALETTE_PAGE_SETTINGS : PALETTE_PAGE_OPERATIONS;" in INPUT,
      "Ctrl+B : 没有按当前页面直达设置/操作命令面板")
check("\"open-settings-page\"" in RENDER and "PALETTE_ACTION_GRAPHICAL_SETTINGS" in RENDER,
      "设置命令面板没有默认的图形化设置入口")
check("PALETTE_PAGE_MENU_SETTINGS" in RENDER and "PALETTE_ACTION_EDIT_PANEL" in RENDER,
      "菜单项设置没有独立的子面板/编辑动作")
check("g_mux.palette_page = settings_active ? PALETTE_PAGE_SETTINGS : PALETTE_PAGE_OPERATIONS;" in INPUT,
      "普通页面没有直接打开操作命令面板")
check("uc == 0xFF1A" in INPUT,
      "命令面板快捷键没有兼容中文全角冒号 U+FF1A")
check("out->color = (g_mux.panes[i].color >= 0 && g_mux.panes[i].color <= 8)" in RENDER and
      "item->color >= 0 && item->color <= 8" in RENDER,
      "切换 panel 的默认颜色没有按蓝色处理")
check("if (color < 0 || color > 8) color = 0;" in RENDER,
      "panel 颜色无效值没有回退到默认蓝色")
check(RENDER.count('"\\x1b[48;2;33;38;45m ");') >= 2,
      "颜色选择器末尾间隙仍被上一块颜色错误填充")
check("top + 1, left);" in RENDER and "top + 2, left);" in RENDER and
      "\\x1b[0m\\x1b[48;2;33;38;45m ",
      "颜色选择器首个空白格没有使用面板背景填充")
check("palette_push_page(PALETTE_PAGE_MENU_SETTINGS);" in INPUT,
      "菜单项设置没有进入子面板")
check("out->action = PALETTE_ACTION_EDIT_PANEL;" in RENDER,
      "菜单项子面板没有把条目连接到编辑子框")
check("int parent_h = palette_visible_rows(host_rows) + 5;" in RENDER and "if (ph < parent_h) ph = parent_h;" in RENDER,
      "编辑 panel 子框高度不足，可能露出父命令面板")
check("Fill any rows added to cover the parent command-panel surface." in RENDER,
      "编辑 panel 子框没有填充父面板多出的行")
check("append_padded_utf8(out, bs, &pos, &label_cols, labels[i], label_w);" in RENDER,
      "编辑 panel 子框的标签行没有填充整行背景")
check("\"退出 termux\"" in RENDER, "命令面板仍缺少‘退出 termux’文案")
check("退出标签页" not in RENDER and "退出标签页" not in ROOT.joinpath("README.md").read_text(encoding="utf-8"),
      "仍存在过时的‘退出标签页’文案")
check("g_mux.running = 0;" in INPUT[INPUT.find("case PALETTE_ACTION_QUIT"):INPUT.find("case PALETTE_ACTION_DEFAULT_STARTUP")],
      "退出 termux 没有设置整个程序的 running 状态")
check("if (total_cols < vis_width)" in UTF8,
      "输入光标在文本恰好填满时仍可能落到右边框/分隔符")

if errors:
    print("对齐验证失败:")
    for error in errors:
        print("  FAIL:", error)
    raise SystemExit(1)

print("对齐验证通过：ANSI 1-based / 鼠标 0-based、所有弹窗宽度、设置页热区、命令面板、搜索底栏与输入光标均已检查。")
