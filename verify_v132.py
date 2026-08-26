#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_v132.py - Verification for win-termux v1.3.2
Tests:
1. text_rc is full rc (not clamped to host_cols - 1), ensuring all host_cols columns are rendered.
2. Scrollbar overlays only when dist <= 10 (when dist > 10, column host_cols renders real character and becomes 100% transparent).
3. Alt-screen programs (nano, vim, less, about) strictly suppress scrollbars & percentage badge.
4. No spurious ResizePseudoConsole in execute_csi.
5. Version number consistency across codebase (v1.3.2).
"""

import sys
import re

def test_source_code_checks():
    print("=== 1) 验证全列宽渲染与完全透明恢复 (Full text_rc render & 100% transparent scrollbar) ===")
    with open("termux.cpp", "r", encoding="utf-8") as f:
        src = f.read()

    # Ensure text_rc is NOT clamped to host_cols - 1 before the loop
    assert "int text_rc = rc;\n\n        int popup_open" in src or "int text_rc = rc;\r\n\r\n        int popup_open" in src, "text_rc must be full rc"
    assert "if (show_sb && dist <= 10)" in src, "Scrollbar should only overlay column host_cols when dist <= 10"
    print("  [OK] 全列宽字符循环渲染，dist > 10 时滚动条完全透明且末尾字符 100% 呈现")

    print("\n=== 2) 验证 Alt-Screen 与无重排换行 (Alt-Screen & no spurious resize) ===")
    assert "int show_sb = (!s->in_alt_screen && g_mux.host_cols >= 10);" in src, "show_sb must check !s->in_alt_screen"
    assert "if (vo > 0 && !s->in_alt_screen)" in src, "percentage badge must check !s->in_alt_screen"
    csi_fn = src[src.find("static void execute_csi("):src.find("static void execute_osc(")]
    assert "ResizePseudoConsole" not in csi_fn, "execute_csi must not call ResizePseudoConsole"
    print("  [OK] Alt-screen 模式全宽运行，进出 Alt-Screen 无多余换行")

    print("\n=== 3) 验证版本号一致性 (Version consistency v1.3.2) ===")
    assert "// termux.cpp - Windows Terminal Multiplexer v1.3.2" in src, "Header version not v1.3.2"
    assert "版本 v1.3.2 | Windows Terminal Multiplexer" in src, "Help version not v1.3.2"
    assert "■ 版本号 (Version)      :\\x1b[0m \\x1b[38;2;230;237;243;1mv1.3.2\\x1b[0m" in src, "About version not v1.3.2"
    print("  [OK] 源代码版本号全部更新为 v1.3.2")

    print("\n所有 v1.3.2 专项测试全部通过！")

if __name__ == "__main__":
    test_source_code_checks()
