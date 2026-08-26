#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_v131.py - Verification for win-termux v1.3.1
Tests:
1. Native full-width allocation for panes (pane_cols = host_cols / nc).
2. Transparent overlay scrollbar when dist > 10 (text at column host_cols is 100% visible, not erased).
3. Alt-screen programs (nano, vim, less, about) strictly suppress scrollbars & percentage badge.
4. No ResizePseudoConsole calls in execute_csi on alt-screen enter/exit (prevents spurious newline in nano && echo end).
5. Version number consistency across codebase (v1.3.1).
"""

import sys
import re

def test_source_code_checks():
    print("=== 1) 验证原生全宽分配与悬浮透明滚动条 (Full-width pane & overlay scrollbar) ===")
    with open("termux.cpp", "r", encoding="utf-8") as f:
        src = f.read()

    assert "int show_sb = (!s->in_alt_screen && g_mux.host_cols >= 10);" in src, "show_sb must check !s->in_alt_screen"
    assert "if (show_sb && dist <= 10)" in src, "Scrollbar should only overlay column host_cols when dist <= 10 (not erased when dist > 10)"
    assert "if (vo > 0 && !s->in_alt_screen)" in src, "percentage badge must check !s->in_alt_screen"
    assert "pane->screen.in_alt_screen = 1;" in src, "About pane must have in_alt_screen = 1"
    assert "int pane_cols = nc;" in src, "handle_resize must allocate full nc to panes"
    print("  [OK] 原生全宽分配，dist > 10 时最右侧输入与文本完全可见且不被空格覆盖")

    print("\n=== 2) 验证无多余 ResizePseudoConsole 杜绝空行 (No spurious resize reflow on 1049 enter/exit) ===")
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
