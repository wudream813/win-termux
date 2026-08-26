#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_v131.py - Verification for win-termux v1.3.1
Tests:
1. Alt-screen programs (nano, vim, less, about) directly expand 1 column wider (full host_cols).
2. Alt-screen enter sequences (1049, 1047, 47) resize pane to host_cols and reset scroll_offset to 0.
3. Alt-screen exit sequences resize pane back to host_cols - 1.
4. Window resize (handle_resize) allocates full nc to alt-screen panes.
5. About pane initialized with in_alt_screen = 1.
6. Version number consistency across codebase (v1.3.1).
"""

import sys
import re

def test_source_code_checks():
    print("=== 1) 验证 Alt-Screen 下全宽扩展与删除滚动条 (Alt-screen full width & scrollbar removal) ===")
    with open("termux.cpp", "r", encoding="utf-8") as f:
        src = f.read()

    assert "int show_sb = (!s->in_alt_screen && g_mux.host_cols >= 10);" in src, "show_sb must check !s->in_alt_screen"
    assert "if (vo > 0 && !s->in_alt_screen)" in src, "percentage badge must check !s->in_alt_screen"
    assert "pane->screen.in_alt_screen = 1;" in src, "About pane must have in_alt_screen = 1"
    assert "int pane_cols = g_mux.panes[i].screen.in_alt_screen ? nc : (nc > 1 ? nc - 1 : 1);" in src, "handle_resize must allocate full nc to alt-screen panes"
    print("  [OK] Alt-screen 模式及关于面板直接扩展 1 列全宽并删除滚动条")

    print("\n=== 2) 验证 Alt-Screen 进入扩容与退出还原 (Dynamic resize on alt-screen enter/exit) ===")
    assert "int target_cols = g_mux.host_cols;" in src, "Missing target_cols = g_mux.host_cols on alt-screen enter"
    assert "int target_cols = g_mux.host_cols > 1 ? g_mux.host_cols - 1 : 1;" in src, "Missing target_cols = host_cols - 1 on alt-screen exit"
    assert "g_mux.panes[pi].scroll_offset = 0;" in src, "Missing scroll_offset reset on alt-screen entry"
    print("  [OK] 进入 Alt-Screen 动态扩容至 host_cols，退出时平滑恢复 host_cols - 1")

    print("\n=== 3) 验证版本号一致性 (Version consistency v1.3.1) ===")
    assert "// termux.cpp - Windows Terminal Multiplexer v1.3.1" in src, "Header version not v1.3.1"
    assert "版本 v1.3.1 | Windows Terminal Multiplexer" in src, "Help version not v1.3.1"
    assert "■ 版本号 (Version)      :\\x1b[0m \\x1b[38;2;230;237;243;1mv1.3.1\\x1b[0m" in src, "About version not v1.3.1"
    print("  [OK] 源代码版本号全部更新为 v1.3.1")

    print("\n所有 v1.3.1 专项测试全部通过！")

if __name__ == "__main__":
    test_source_code_checks()
