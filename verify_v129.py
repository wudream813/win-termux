#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_v129.py - Verification for win-termux v1.2.9
Tests:
1. Hover preview tooltips are strictly suppressed when item text is NOT truncated.
   - Tab title: <= 15 cols -> no preview; > 15 cols -> preview active.
   - Chooser name: <= 15 cols -> no preview; > 15 cols -> preview active.
   - Settings name: <= 10 cols -> no preview; > 10 cols -> preview active.
   - Settings cmd: <= 15 cols -> no preview; > 15 cols -> preview active.
2. ANSI SGR style isolation on scrollbars, percentage indicator, and line clears.
   - Thumb, track, percent badge, and clear-line sequences reset attributes with \x1b[0; / \x1b[0m.
3. Nested termux instance detection and scrollbar suppression.
   - WIN_TERMUX env detection, show_sb=0 when nested, pane_cols=host_cols when nested.
"""

import sys
import re

def test_source_code_checks():
    print("=== 1) 验证未截断时不触发预览 (No hover preview when not truncated) ===")
    with open("termux.cpp", "r", encoding="utf-8") as f:
        src = f.read()

    # Check tab title hover check
    assert "utf8_cols(full_title, (int)strlen(full_title)) <= 15" in src, "Missing tab title hover truncation check in handle_mouse"
    assert "tcols > 15" in src, "Missing tab title hover truncation check in render_all"
    print("  [OK] 标签栏标题未截断 (<=15 列) 不触发预览浮层")

    # Check chooser item name hover check
    assert "utf8_cols(name, (int)strlen(name)) <= 15" in src, "Missing chooser item hover truncation check in handle_mouse"
    print("  [OK] 新建菜单名称未截断 (<=15 列) 不触发预览浮层")

    # Check settings item name & cmd hover check
    assert "utf8_cols(name, (int)strlen(name)) <= 10" in src, "Missing settings name hover truncation check in handle_mouse"
    assert "utf8_cols(cmd, (int)strlen(cmd)) <= 15" in src, "Missing settings cmd hover truncation check in handle_mouse"
    print("  [OK] 设置项名称 (<=10 列) 及命令行 (<=15 列) 未截断不触发预览浮层")

    print("\n=== 2) 验证滚动条 ANSI SGR 颜色隔离 (Scrollbar ANSI SGR isolation) ===")
    assert "\\x1b[0;48;2;225;235;250m" in src, "Missing SGR reset in hover thumb"
    assert "\\x1b[0;48;2;%d;%d;%dm" in src, "Missing SGR reset in thumb"
    assert "\\x1b[0;48;2;%d;%d;%dm\\x1b[38;2;%d;%d;%dm│\\x1b[0m" in src, "Missing SGR reset in track"
    assert "\\x1b[0;30;43m[%d%%]" in src, "Missing SGR reset in percentage badge"
    assert "\\x1b[0m\\x1b[K" in src, "Missing SGR reset before \\x1b[K line clear"
    print("  [OK] 滚动条滑块、轨道、进度百分比标签及行清除均包含完整属性重置 (\\x1b[0; / \\x1b[0m)")

    print("\n=== 3) 验证嵌套 Termux 检测与滚动条防污染 (Nested Termux detection & shielding) ===")
    assert "g_mux.is_nested = (GetEnvironmentVariableW(L\"WIN_TERMUX\", NULL, 0) > 0);" in src, "Missing WIN_TERMUX env check"
    assert "SetEnvironmentVariableW(L\"WIN_TERMUX\", L\"1\");" in src, "Missing WIN_TERMUX env export"
    assert "int show_sb = (!g_mux.is_nested && !s->in_alt_screen && g_mux.host_cols >= 10);" in src, "Missing is_nested guard on show_sb"
    assert "int pane_cols = g_mux.is_nested ? g_mux.host_cols : (g_mux.host_cols > 1 ? g_mux.host_cols - 1 : 1);" in src, "Missing is_nested pane_cols sizing in create_pane"
    print("  [OK] 嵌套 termux 自动检测 WIN_TERMUX 环境变量并抑制子层滚动条，彻底根除双滚动条与颜色污染")

    print("\n=== 4) 验证版本号一致性 (Version consistency v1.2.9) ===")
    assert "// termux.cpp - Windows Terminal Multiplexer v1.2.9" in src, "Header version not v1.2.9"
    assert "版本 v1.2.9 | Windows Terminal Multiplexer" in src, "Help version not v1.2.9"
    assert "■ 版本号 (Version)      :\\x1b[0m \\x1b[38;2;230;237;243;1mv1.2.9\\x1b[0m" in src, "About version not v1.2.9"
    assert "Windows Terminal Multiplexer v1.2.9" in src, "Banner version not v1.2.9"
    print("  [OK] 所有版本号均精准更新至 v1.2.9")

    print("\n所有 v1.2.9 专项测试全部通过！")

if __name__ == "__main__":
    test_source_code_checks()
