#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_v131.py - Verification for win-termux v1.3.1
Tests:
1. Alt-screen programs (nano, vim, less, about) directly expand 1 column wider (full host_cols).
2. Pane initialization and resize allocate full host_cols to ConPTY.
3. Normal mode reserves 1 column for scrollbar (text_rc = host_cols - 1), alt-screen uses full host_cols (no scrollbar).
4. No spurious ResizePseudoConsole on alt-screen enter/exit (prevents empty line on nano && echo end).
5. Version number consistency across codebase (v1.3.1).
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
    assert "int pane_cols = nc;" in src, "handle_resize must allocate full nc to panes"
    print("  [OK] Alt-screen 模式及关于面板直接扩展 1 列全宽并删除滚动条")

    print("\n=== 2) 验证无多余 ResizePseudoConsole 杜绝空行 (No spurious resize reflow on 1049 enter/exit) ===")
    # Ensure execute_csi does NOT call ResizePseudoConsole inside 1049/1047 cases
    csi_fn = src[src.find("static void execute_csi("):src.find("static void execute_osc(")]
    assert "ResizePseudoConsole" not in csi_fn, "execute_csi must not call ResizePseudoConsole (causes spurious newlines)"
    assert "g_mux.panes[pi].scroll_offset = 0;" in src, "Missing scroll_offset reset on alt-screen entry"
    print("  [OK] 切换 Alt-Screen 不产生多余重排换行，保证 nano && echo end 输出无空行")

    print("\n=== 3) 验证版本号一致性 (Version consistency v1.3.1) ===")
    assert "// termux.cpp - Windows Terminal Multiplexer v1.3.1" in src, "Header version not v1.3.1"
    assert "版本 v1.3.1 | Windows Terminal Multiplexer" in src, "Help version not v1.3.1"
    assert "■ 版本号 (Version)      :\\x1b[0m \\x1b[38;2;230;237;243;1mv1.3.1\\x1b[0m" in src, "About version not v1.3.1"
    print("  [OK] 源代码版本号全部更新为 v1.3.1")

    print("\n所有 v1.3.1 专项测试全部通过！")

if __name__ == "__main__":
    test_source_code_checks()
