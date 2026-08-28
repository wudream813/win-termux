#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Regression tests for palette menu-item management and modal guards.

The reorder/delete helpers are compiled from src/input.c with a deliberately
small harness.  The surrounding assertions then pin the production palette
wiring: the management subpanel contains existing entries only, reorder is
blocked while filtering, numbering is based on filtered rank, and text input
cannot hand Ctrl+B through to the global prefix handler.
"""

from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent
INPUT = (ROOT / "src" / "input.c").read_text(encoding="utf-8")
RENDER = (ROOT / "src" / "render.c").read_text(encoding="utf-8")


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


HELPERS = "\n\n".join(
    extract_func(INPUT, prefix)
    for prefix in ("static int palette_move_menu_item", "static int palette_delete_menu_item")
)

PRELUDE = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { PALETTE_PAGE_MENU_SETTINGS = 7 };
typedef struct { char name[32]; char cmd[256]; char workdir[256]; } ChooserItem;
static ChooserItem g_chooser_items[8];
static int g_chooser_item_count;
static int save_calls;
static struct {
    int palette_page;
    int palette_query_len;
    int palette_sel;
    int palette_scroll;
    int needs_redraw;
} g_mux;
void save_config(void) { save_calls++; }
"""

DRIVER = r"""
static void names(char *out, size_t cap) {
    if (g_chooser_item_count == 1)
        snprintf(out, cap, "%s", g_chooser_items[0].name);
    else if (g_chooser_item_count == 2)
        snprintf(out, cap, "%s,%s", g_chooser_items[0].name,
                 g_chooser_items[1].name);
    else
        snprintf(out, cap, "%s,%s,%s", g_chooser_items[0].name,
                 g_chooser_items[1].name, g_chooser_items[2].name);
}

int main(void) {
    int filtered[] = {0, 1, 2};
    char got[128];
    g_mux.palette_page = PALETTE_PAGE_MENU_SETTINGS;
    g_mux.palette_query_len = 0;
    g_mux.palette_sel = 1;
    g_mux.palette_scroll = 0;
    g_chooser_item_count = 3;
    strcpy(g_chooser_items[0].name, "A");
    strcpy(g_chooser_items[1].name, "B");
    strcpy(g_chooser_items[2].name, "C");

    assert(palette_move_menu_item(filtered, 3, 1, -1) == 1);
    names(got, sizeof(got));
    assert(strcmp(got, "B,A,C") == 0);
    assert(g_mux.palette_sel == 0);
    assert(save_calls == 1);

    /* A filtered result is only a view: U/D cannot change source order. */
    g_mux.palette_query_len = 1;
    g_mux.palette_sel = 0;
    assert(palette_move_menu_item(filtered, 3, 0, 1) == 0);
    names(got, sizeof(got));
    assert(strcmp(got, "B,A,C") == 0);
    assert(save_calls == 1);

    /* Delete uses the selected filtered item's source index and persists. */
    g_mux.palette_query_len = 1;
    g_mux.palette_sel = 1;
    int filtered_after_move[] = {0, 1, 2};
    assert(palette_delete_menu_item(filtered_after_move, 3, 1) == 1);
    names(got, sizeof(got));
    assert(strcmp(got, "B,C") == 0);
    assert(g_chooser_item_count == 2);
    assert(save_calls == 2);

    g_chooser_item_count = 1;
    g_mux.palette_sel = 0;
    assert(palette_delete_menu_item(filtered_after_move, 1, 0) == 0);
    assert(save_calls == 2);
    puts("menu settings regression passed: reorder, filtered disable and delete are stable.");
    return 0;
}
"""


def main() -> int:
    errors = []
    def check(condition: bool, message: str) -> None:
        if not condition:
            errors.append(message)

    operation_start = RENDER.find("g_palette_operation_items")
    settings_start = RENDER.find("g_palette_setting_items")
    startup_start = RENDER.find("g_palette_startup_items")
    operation_items = RENDER[operation_start:settings_start]
    setting_items = RENDER[settings_start:startup_start]
    menu_start = RENDER.find("if (page == PALETTE_PAGE_MENU_SETTINGS)")
    menu_end = RENDER.find("    return 0;", menu_start)
    menu_info = RENDER[menu_start:menu_end]
    row_start = RENDER.find("static void render_palette_item_row")
    row_end = RENDER.find("static void render_palette_editor", row_start)
    row = RENDER[row_start:row_end]

    check('"open-settings-page"' in operation_items and
          "PALETTE_ACTION_GRAPHICAL_SETTINGS" in operation_items,
          "图形化设置没有放入操作命令面板")
    check('"open-settings-page"' not in setting_items and
          "PALETTE_ACTION_GRAPHICAL_SETTINGS" not in setting_items,
          "设置命令面板仍包含图形化设置入口")
    check('"about"' in setting_items and "PALETTE_ACTION_OPEN_ABOUT" in setting_items,
          "设置命令面板没有关于入口")
    check("case PALETTE_ACTION_OPEN_ABOUT" in INPUT and "create_about_pane()" in INPUT,
          "关于入口没有连接到独立 About panel")
    check("return g_chooser_item_count;" in RENDER[RENDER.find("case PALETTE_PAGE_MENU_SETTINGS"):RENDER.find("default:", RENDER.find("case PALETTE_PAGE_MENU_SETTINGS"))],
          "菜单项设置仍为添加条目预留伪条目")
    check('out->id = "add-panel"' not in menu_info and
          'out->title = "添加 panel 条目"' not in menu_info,
          "菜单项设置子面板仍显示添加 panel 条目")
    check("filtered[fi], fi + 1" in RENDER,
          "命令面板序号没有绑定过滤结果排名")
    check("item->number" not in row,
          "命令面板行渲染仍读取静态 item->number")
    check("g_mux.palette_query_len > 0" in HELPERS and
          "!g_mux.palette_query_len" in INPUT,
          "菜单项位置修改没有在搜索期间禁用")
    check("palette_delete_menu_item" in INPUT and "save_config();" in HELPERS,
          "菜单项设置删除操作没有持久化")
    check("static int key_input_modal_active" in INPUT and
          "!key_input_modal_active()" in INPUT and
          "g_mux.prefix_mode = 0;" in INPUT and
          "g_search_active && !g_mux.prefix_mode && !g_copy_mode && !key_input_modal_active()" in INPUT,
          "文本输入模态没有阻断全局快捷键")
    check("wraparound_pending && cy + 1 < rr" not in RENDER and
          "if (cx >= rc) cx = rc - 1;" in RENDER and
          "terminal_cursor_position" in RENDER,
          "终端输出末格光标仍会被 pending wrap 移走或隐藏")

    if errors:
        for error in errors:
            print("FAIL:", error, file=sys.stderr)
        return 1

    code = PRELUDE + "\n" + HELPERS + "\n" + DRIVER
    with tempfile.TemporaryDirectory() as tmp:
        c_file = os.path.join(tmp, "menu_settings.c")
        exe = os.path.join(tmp, "menu_settings")
        Path(c_file).write_text(code, encoding="utf-8")
        result = subprocess.run(
            ["gcc", "-std=c99", "-O2", "-Wall", "-Wextra", c_file, "-o", exe],
            capture_output=True,
            text=True,
        )
        if result.returncode:
            print(result.stderr, file=sys.stderr)
            return 1
        result = subprocess.run([exe], capture_output=True, text=True)
        if result.returncode:
            print(result.stderr, file=sys.stderr)
            return 1
        print(result.stdout.strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
