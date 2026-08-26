#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_v131.py - Verification for win-termux v1.3.1
Tests:
1. Alt-screen programs (nano, vim, less, about) strictly suppress scrollbars & percentage badge.
2. Alt-screen enter sequences (1049, 1047, 47) reset scroll_offset to 0.
3. About pane initialized with in_alt_screen = 1.
4. Version number consistency across codebase (v1.3.1).
"""

import sys
import re

def test_source_code_checks():
    print("=== 1) 验证 Alt-Screen 下严格屏蔽滚动条与进度标签 (Alt-screen scrollbar suppression) ===")
    with open("termux.cpp", "r", encoding="utf-8") as f:
        src = f.read()

    assert "int show_sb = (!s->in_alt_screen && g_mux.host_cols >= 10);" in src, "show_sb must check !s->in_alt_screen"
    assert "if (vo > 0 && !s->in_alt_screen)" in src, "percentage badge must check !s->in_alt_screen"
    assert "pane->screen.in_alt_screen = 1;" in src, "About pane must have in_alt_screen = 1"
    print("  [OK] Alt-screen 模式及关于面板严格禁用滚动条与进度标签")

    print("\n=== 2) 验证 Alt-Screen 指令捕获与滚动偏移清零 (CSI alt-screen enter/exit handling) ===")
    assert "case 47: case 1047:" in src, "Missing 47/1047 handling"
    assert "case 1049:" in src, "Missing 1049 handling"
    assert "g_mux.panes[pi].scroll_offset = 0;" in src, "Missing scroll_offset reset on alt-screen entry"
    print("  [OK] 完整捕获 1049/1047/47 进入/退出，并在进入时强制归零 scroll_offset")

    print("\n=== 3) 验证版本号一致性 (Version consistency v1.3.1) ===")
    assert "// termux.cpp - Windows Terminal Multiplexer v1.3.1" in src, "Header version not v1.3.1"
    assert "版本 v1.3.1 | Windows Terminal Multiplexer" in src, "Help version not v1.3.1"
    assert "■ 版本号 (Version)      :\\x1b[0m \\x1b[38;2;230;237;243;1mv1.3.1\\x1b[0m" in src, "About version not v1.3.1"
    print("  [OK] 源代码版本号全部更新为 v1.3.1")

    print("\n所有 v1.3.1 专项测试全部通过！")

if __name__ == "__main__":
    test_source_code_checks()
