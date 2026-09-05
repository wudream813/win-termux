#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Regression coverage for the v1.8.3 layered command palette.

This check combines source-level wiring assertions with two tiny harnesses:
one exercises the parent-page view snapshot and one decodes the production
color-picker row into terminal cells.  It deliberately checks behavior at the
same boundaries users see: visible numbering after scrolling, Tab focus,
filtered reorder rules, Esc restoration, and every picker cell's final
background.
"""

from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent
INPUT = (ROOT / "src" / "input.c").read_text(encoding="utf-8")
RENDER = (ROOT / "src" / "render.c").read_text(encoding="utf-8")
TYPES = (ROOT / "include" / "types.h").read_text(encoding="utf-8")
RENDER_H = (ROOT / "include" / "render.h").read_text(encoding="utf-8")


def extract_func(text: str, prefix: str) -> str:
    start = text.find(prefix)
    if start < 0:
        raise RuntimeError(f"function not found: {prefix}")
    brace = text.find("{", start)
    if brace < 0:
        raise RuntimeError(f"function body not found: {prefix}")
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    raise RuntimeError(f"unterminated function: {prefix}")


def section(text: str, start_marker: str, end_marker: str) -> str:
    start = text.find(start_marker)
    end = text.find(end_marker, start + len(start_marker))
    if start < 0 or end < 0:
        return ""
    return text[start:end]


def parse_row(line: str):
    """Decode one picker row into (character, background, foreground) cells."""
    sgr = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
    i = 0
    bg = None
    fg = None
    cells = []
    while i < len(line):
        match = sgr.match(line, i)
        if match:
            seq = match.group(0)
            if seq.endswith("m"):
                body = seq[2:-1]
                if body == "0":
                    bg = None
                    fg = None
                elif body.startswith(("48;2;", "048;2;")):
                    # 界面配色使用零填充写法（主题重映射的判定依据），这里统一按十进制解析
                    values = [int(v) for v in body.split(";")[2:]]
                    bg = tuple(values[:3])
                elif body.startswith(("38;2;", "038;2;")):
                    values = [int(v) for v in body.split(";")[2:]]
                    fg = tuple(values[:3])
            i = match.end()
            continue
        cells.append((line[i], bg, fg))
        i += 1
    return cells


STACK_FUNCTIONS = "\n\n".join(
    extract_func(INPUT, prefix)
    for prefix in (
        "static void palette_close",
        "static void palette_reset_query",
        "static void palette_push_page",
        "static void palette_pop_page",
    )
)

STACK_PRELUDE = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define PALETTE_STACK_MAX 8
#define PALETTE_PAGE_ROOT 0
#define PALETTE_FOCUS_INPUT 0
#define PALETTE_FOCUS_LIST 1

typedef struct {
    int page;
    int selection;
    int scroll;
    int query_len;
    int query_pos;
    int focus;
    char query[64];
} PaletteViewState;

typedef struct {
    int palette_mode;
    int palette_page;
    PaletteViewState palette_stack[PALETTE_STACK_MAX];
    int palette_stack_len;
    int palette_sel;
    char palette_query[64];
    int palette_query_len;
    int palette_query_pos;
    int palette_scroll;
    int palette_focus;
    int palette_field;
    int palette_edit_idx;
    int palette_edit_new;
    int needs_redraw;
} MuxState;

static MuxState g_mux;
"""

STACK_DRIVER = r"""
int main(void) {
    g_mux.palette_mode = 1;
    g_mux.palette_page = 4;
    g_mux.palette_sel = 6;
    g_mux.palette_scroll = 3;
    g_mux.palette_query_len = 5;
    g_mux.palette_query_pos = 2;
    g_mux.palette_focus = PALETTE_FOCUS_LIST;
    memcpy(g_mux.palette_query, "alpha", 6);

    palette_push_page(9);
    assert(g_mux.palette_stack_len == 1);
    assert(g_mux.palette_page == 9);
    assert(g_mux.palette_sel == 0);
    assert(g_mux.palette_scroll == 0);
    assert(g_mux.palette_query_len == 0);
    assert(g_mux.palette_query_pos == 0);
    assert(g_mux.palette_query[0] == 0);
    assert(g_mux.palette_focus == PALETTE_FOCUS_INPUT);

    g_mux.palette_sel = 1;
    g_mux.palette_scroll = 1;
    g_mux.palette_query_len = 1;
    g_mux.palette_query_pos = 1;
    g_mux.palette_query[0] = 'x';
    g_mux.palette_focus = PALETTE_FOCUS_INPUT;
    palette_pop_page();

    assert(g_mux.palette_stack_len == 0);
    assert(g_mux.palette_page == 4);
    assert(g_mux.palette_sel == 6);
    assert(g_mux.palette_scroll == 3);
    assert(g_mux.palette_query_len == 5);
    assert(g_mux.palette_query_pos == 2);
    assert(strcmp(g_mux.palette_query, "alpha") == 0);
    assert(g_mux.palette_focus == PALETTE_FOCUS_LIST);
    puts("palette view snapshot regression passed: Esc restores parent selection, scroll, query and focus.");
    return 0;
}
"""

COLOR_FUNCTIONS = "\n\n".join(
    extract_func(RENDER, prefix)
    for prefix in ("static void render_color_picker_cell", "static void render_color_picker_row")
)

COLOR_PRELUDE = r"""
#include <stdio.h>
#include <string.h>

#define CP_W 20
static int g_mouse_x;
static int g_mouse_y;
#define CP_SWATCH_W 3
static const char *const TAB_COLOR_BG_DIM[9] = {
    "\x1b[48;2;22;62;128m",
    "\x1b[48;2;22;62;128m",
    "\x1b[48;2;36;99;49m",
    "\x1b[48;2;110;82;30m",
    "\x1b[48;2;74;48;122m",
    "\x1b[48;2;24;80;48m",
    "\x1b[48;2;52;96;128m",
    "\x1b[48;2;112;66;34m",
    "\x1b[48;2;104;50;90m",
};
static const char *const TAB_COLOR_BG[9] = {
    "\x1b[48;2;31;111;235m",
    "\x1b[48;2;31;111;235m",
    "\x1b[48;2;63;185;80m",
    "\x1b[48;2;210;153;34m",
    "\x1b[48;2;137;87;229m",
    "\x1b[48;2;31;136;61m",
    "\x1b[48;2;121;192;255m",
    "\x1b[48;2;217;119;54m",
    "\x1b[48;2;205;93;173m",
};
"""

COLOR_DRIVER = r"""
int main(void) {
    char out[16384];
    int pos;

    /* Hover the number in the third swatch of row one. */
    g_mouse_x = 10 + 1 + 2 * 4;
    g_mouse_y = 2;
    pos = 0;
    render_color_picker_row(out, sizeof(out), &pos, 3, 10, 1);
    fwrite(out, 1, pos, stdout);
    fputc('\n', stdout);

    g_mouse_x = -1;
    g_mouse_y = -1;
    pos = 0;
    render_color_picker_row(out, sizeof(out), &pos, 4, 10, 5);
    fwrite(out, 1, pos, stdout);
    fputc('\n', stdout);
    return 0;
}
"""


def run_harness(code: str, name: str, compiler="gcc") -> subprocess.CompletedProcess:
    with tempfile.TemporaryDirectory() as tmp:
        c_file = os.path.join(tmp, name + ".c")
        exe = os.path.join(tmp, name)
        Path(c_file).write_text(code, encoding="utf-8")
        result = subprocess.run(
            [compiler, "-std=c99", "-O2", "-Wall", "-Wextra", c_file, "-o", exe],
            capture_output=True,
            text=True,
        )
        if result.returncode:
            return result
        return subprocess.run([exe], capture_output=True, text=True)


def main() -> int:
    errors = []
    def check(condition: bool, message: str) -> None:
        if not condition:
            errors.append(message)

    operation = section(RENDER, "g_palette_operation_items", "g_palette_setting_items")
    settings = section(RENDER, "g_palette_setting_items", "g_palette_startup_items")
    menu_info = section(RENDER, "if (page == PALETTE_PAGE_MENU_SETTINGS)", "return 0;")
    key_handler = extract_func(INPUT, "void handle_palette_key")
    cursor_palette = section(RENDER, "} else if (g_mux.palette_mode)", "} else if (g_copy_mode)")

    check("PALETTE_FOCUS_INPUT" in RENDER_H and "PALETTE_FOCUS_LIST" in RENDER_H and
          "palette_focus" in TYPES and "PaletteViewState palette_stack" in TYPES,
          "MuxState 没有独立命令面板焦点或父页面视图快照")
    check('"open-settings-page"' in operation and
          "PALETTE_ACTION_GRAPHICAL_SETTINGS" in operation,
          "图形化设置入口不在操作命令面板")
    check('"open-settings-page"' not in settings and
          "PALETTE_ACTION_GRAPHICAL_SETTINGS" not in settings,
          "设置命令面板仍包含图形化设置入口")
    check('"about"' in operation and "PALETTE_ACTION_OPEN_ABOUT" in operation and
          '"about"' not in settings and "PALETTE_ACTION_OPEN_ABOUT" not in settings,
          "关于没有严格只放在操作命令面板")
    check("case PALETTE_ACTION_OPEN_ABOUT" in INPUT and "create_about_pane()" in INPUT,
          "关于没有打开独立 About panel")
    check("return g_chooser_item_count;" in RENDER and
          'out->id = "add-panel"' not in menu_info and
          'out->title = "添加 panel 条目"' not in menu_info,
          "菜单项设置仍包含添加 panel 条目")

    check("#define PALETTE_MAX_VISIBLE 9" in RENDER and
          "int visible = PALETTE_MAX_VISIBLE;" in RENDER and
          "filtered[fi], vi + 1" in RENDER,
          "命令面板没有按当前可见窗口限制为 9 并重新编号")
    check("g_mux.palette_scroll + number - 1" in key_handler and
          "int visible_count = count - g_mux.palette_scroll" in key_handler and
          "g_mux.palette_focus == PALETTE_FOCUS_LIST" in key_handler and
          "vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9" in key_handler,
          "数字快捷键没有按当前可见窗口/结果焦点工作")
    jump_block = key_handler[key_handler.find("int jump_to_list ="):] if "int jump_to_list =" in key_handler else ""
    check("g_mux.palette_focus == PALETTE_FOCUS_INPUT && g_mux.palette_query_len == 0" in key_handler and
          jump_block and
          "vk == VK_UP || vk == VK_DOWN" in jump_block and
          "vk == VK_PRIOR || vk == VK_NEXT" in jump_block and
          "vk == VK_LEFT || vk == VK_RIGHT" in jump_block and
          "vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9" in jump_block and
          "g_mux.palette_focus = PALETTE_FOCUS_LIST;" in jump_block and
          key_handler.find("int jump_to_list =") < key_handler.find("palette_move_menu_item"),
          "刚打开命令面板时方向键/数字没有直接把焦点切到结果列表")
    check('g_mux.palette_query_len == 0) ? "选当前" : "搜索"' in RENDER,
          "空查询时底部提示没有把数字标注为选当前")
    check("if (vk == VK_TAB)" in key_handler and
          "PALETTE_FOCUS_LIST" in key_handler and
          "PALETTE_FOCUS_INPUT" in key_handler,
          "命令面板没有 Tab 输入框/结果框焦点切换")
    check("palette_input_bg = g_mux.palette_focus == PALETTE_FOCUS_INPUT" in RENDER and
          "g_mux.palette_focus == PALETTE_FOCUS_INPUT" in cursor_palette and
          "\\x1b[?25l" in cursor_palette,
          "命令面板没有焦点视觉或结果焦点隐藏输入光标")

    push = extract_func(INPUT, "static void palette_push_page")
    pop = extract_func(INPUT, "static void palette_pop_page")
    check(all(token in push for token in (
        "saved->page", "saved->selection", "saved->scroll", "saved->query_len",
        "saved->query_pos", "saved->focus", "memcpy(saved->query")),
          "进入子面板前没有保存完整父页面视图状态")
    check(all(token in pop for token in (
        "g_mux.palette_page = saved->page", "g_mux.palette_sel = saved->selection",
        "g_mux.palette_scroll = saved->scroll", "g_mux.palette_query_len = saved->query_len",
        "g_mux.palette_query_pos = saved->query_pos", "g_mux.palette_focus = saved->focus",
        "memcpy(g_mux.palette_query")),
          "Esc 返回没有恢复完整父页面视图状态")
    check("palette_reset_query();" not in pop,
          "Esc 返回子面板仍会重置父页面查询/选择")

    check("g_mux.palette_focus == PALETTE_FOCUS_LIST && has_ctrl" in key_handler and
          "vk == VK_UP || vk == VK_DOWN" in key_handler,
          "菜单项排序没有绑定 Ctrl+↑/↓ 与结果焦点")
    move = extract_func(INPUT, "static int palette_move_menu_item")
    check("g_mux.palette_query_len > 0" in move,
          "菜单项搜索期间仍可能排序")
    check("!has_ctrl && !g_mux.palette_query_len" not in key_handler and
          "is_up = (uc == 'u'" not in key_handler and
          "is_down = (uc == 'd'" not in key_handler,
          "U/D 仍被旧的无 Ctrl 排序快捷键拦截")
    check("g_mux.palette_focus != PALETTE_FOCUS_INPUT" in key_handler and
          "if (uc)" in key_handler,
          "结果焦点与输入焦点的字符处理没有明确分离")

    check("if (g_mux.palette_mode)" in INPUT and
          INPUT.find("if (g_mux.palette_mode)") < INPUT.find("if (g_mux.prefix_mode)"),
          "命令面板输入模态没有在全局前缀处理前被优先消费")
    check("!key_input_modal_active()" in INPUT and
          "g_mux.prefix_mode = 0;" in INPUT,
          "文本输入模态没有清理/阻断待处理全局前缀")

    check("render_color_picker_cell" in RENDER and
          "int interior_cols = 2 + 4 * 4;" in RENDER and
          "while (interior_cols < CP_W - 1)" in RENDER,
          "颜色选择器没有逐单元格封闭面板背景")
    check("CP_SWATCH_W" in RENDER_H and
          "if (w >= CP_SWATCH_W)" in RENDER and
          "int cols = utf8_cols(hdr" in RENDER and
          'palette_hline(out, bs, &pos, top + 3, left, CP_W, "└", "┘")' in RENDER,
          "颜色选择器面板没有按 CP_W 自适应收窄（右侧留大片空白）")
    # 退出确认弹窗已泛化为通用确认弹窗 render_confirm_dialog(kind)：kind=0 退出、
    # kind=1 关闭窗格（confirm_on_close）；render_confirm_exit 是 kind=0 的包装。
    confirm_render = extract_func(RENDER, "void render_confirm_dialog")
    confirm_geom = extract_func(RENDER, "void confirm_exit_button_geom")
    check("render_confirm_exit" in RENDER and "render_confirm_dialog" in RENDER and
          "confirm_exit_button_geom" in RENDER_H and
          "CONFIRM_YES_W" in confirm_geom and "CONFIRM_NO_W" in confirm_geom and
          "confirm_exit_button_geom(host_rows, host_cols, &row" in confirm_render and
          "confirm_pad(out, bs, &pos, interior - msg_cols)" in confirm_render and
          "confirm_pad(out, bs, &pos, (left + w - 1) - ne)" in confirm_render,
          "确认对话框没有按面板宽度补齐（右边框会错位）")
    check("handle_confirm_dialog_mouse" in INPUT and
          "confirm_exit_button_geom(g_mux.host_rows, g_mux.host_cols" in INPUT and
          "if (!g_mouse_enabled) return;" in INPUT and
          "do_close_current_pane()" in INPUT,
          "关闭窗格确认对话框没有正确接线鼠标 / 关闭动作")
    check("int yes_hover = (mrow == row && mcol >= ys && mcol < ye);" in confirm_render and
          "int no_hover = (mrow == row && mcol >= ns && mcol < ne);" in confirm_render and
          'kind ? " 确定要关闭当前窗格吗？"' in confirm_render,
          "确认按钮没有 hover 高亮 / 缺少关闭窗格文案")
    check("dc = c - (left + 2)" in INPUT and
          "g_mouse_x >= left + 1 + i * 4" in RENDER and
          "int mouse_row = row - 1" in RENDER,
          "颜色选择器渲染、hover、鼠标命中坐标没有统一")

    if errors:
        for error in errors:
            print("FAIL:", error, file=sys.stderr)
        return 1

    result = run_harness(STACK_PRELUDE + "\n" + STACK_FUNCTIONS + "\n" + STACK_DRIVER,
                         "palette_stack")
    if result.returncode:
        print(result.stderr or result.stdout, file=sys.stderr)
        return 1
    print(result.stdout.strip())

    result = run_harness(COLOR_PRELUDE + "\n" + COLOR_FUNCTIONS + "\n" + COLOR_DRIVER,
                         "palette_color")
    if result.returncode:
        print(result.stderr or result.stdout, file=sys.stderr)
        return 1
    rows = result.stdout.splitlines()
    if len(rows) != 2:
        print("FAIL: 颜色选择器行 harness 没有输出两行", file=sys.stderr)
        return 1

    panel = (33, 38, 45)
    colors = {
        1: (31, 111, 235), 2: (63, 185, 80), 3: (210, 153, 34),
        4: (137, 87, 229), 5: (31, 136, 61), 6: (121, 192, 255),
        7: (217, 119, 54), 8: (205, 93, 173),
    }
    dim = {
        1: (22, 62, 128), 2: (36, 99, 49), 3: (110, 82, 30), 4: (74, 48, 122),
        5: (24, 80, 48), 6: (52, 96, 128), 7: (112, 66, 34), 8: (104, 50, 90),
    }

    def swatch(index, hovered):
        """3 coloured cells (bright only while hovered) plus a panel gap cell."""
        base = colors[index] if hovered else dim[index]
        return [base] * 3 + [panel]

    expected_backgrounds = (
        [panel, panel] + sum((swatch(i, i == 3) for i in range(1, 5)), []) + [panel] * 2,
        [panel, panel] + sum((swatch(i, False) for i in range(5, 9)), []) + [panel] * 2,
    )
    for row_index, (line, expected) in enumerate(zip(rows, expected_backgrounds), start=1):
        cells = parse_row(line)
        if len(cells) != 20:
            print(f"FAIL: 颜色选择器第{row_index}行只渲染 {len(cells)} 个单元格（应为 20）", file=sys.stderr)
            return 1
        got = [cell[1] for cell in cells]
        if got != expected:
            print(f"FAIL: 颜色选择器第{row_index}行背景状态不闭合", file=sys.stderr)
            print(f"  got={got}", file=sys.stderr)
            print(f"  want={expected}", file=sys.stderr)
            return 1

    first = parse_row(rows[0])
    white = (255, 255, 255)
    idle = (139, 148, 158)
    # Third swatch's number is the 12th cell: border + padding + 2*4 + 1.
    if first[11][2] != white:
        print("FAIL: hover 没有只高亮第三个色块的数字", file=sys.stderr)
        return 1
    for index, cell in enumerate(first):
        if index != 11 and cell[2] == white:
            print(f"FAIL: hover 意外高亮了第 {index + 1} 个单元格", file=sys.stderr)
            return 1
    if any(cell[2] != idle for cell in parse_row(rows[1]) if cell[0].isdigit()):
        print("FAIL: 第二行非 hover 色块数字没有用未激活标签页的灰色前景", file=sys.stderr)
        return 1

    print("color picker regression passed: 8 contiguous swatches, hover and final backgrounds are exact.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
