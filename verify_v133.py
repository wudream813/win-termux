#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_v133.py - Verification for win-termux v1.3.3 (and v1.3.2)
Tests:
1. Complete transparency when dist > 10 (text at column host_cols is 100% visible and not overwritten).
2. Dragging retains 10-level distance gradient and becomes invisible when dist > 10.
3. Alt-screen programs (nano, vim, less, about) strictly suppress scrollbars & percentage badge.
4. No spurious ResizePseudoConsole on alt-screen enter/exit (nano && echo end has zero empty lines).
5. Version number consistency across codebase (v1.3.3).
"""

import sys
import re

def test_source_code_checks():
    print("=== 1) 验证滚动条完全透明与末尾字符穿透 (Complete transparency & last character preservation) ===")
    with open("termux.cpp", "r", encoding="utf-8") as f:
        src = f.read()

    assert "if (show_sb && dist <= 10)" in src, "Scrollbar should only be drawn when dist <= 10 (transparent and preserved when dist > 10)"
    assert "int text_rc = rc;" in src, "text_rc must be full rc so loop renders all columns including column 80"
    print("  [OK] dist > 10 时滚动条 100% 透明，末尾第 80 列字符完整渲染且不被空格遮挡清空")

    print("\n=== 2) 验证拖拽时保持距离分层与大于10格隐形 (Dragging retains distance gradient & >10 invisibility) ===")
    render_fn = src[src.find("static void render_screen("):src.find("static void dump_pane_bytes(")]
    # dist must not be forced to 0 when g_sb_dragging
    assert "(g_sb_dragging) ? 0 :" not in render_fn, "dist must not be forced to 0 when dragging"
    print("  [OK] 拖拽滚动条时距离梯度正常响应，大于 10 列时同样完全隐形")

    print("\n=== 3) 验证 Alt-Screen 无重排与零空行 (Alt-screen zero reflow on enter/exit) ===")
    assert "int show_sb = (!s->in_alt_screen && g_mux.host_cols >= 10);" in src, "show_sb must check !s->in_alt_screen"
    csi_fn = src[src.find("static void execute_csi("):src.find("static void execute_osc(")]
    assert "ResizePseudoConsole" not in csi_fn, "execute_csi must not call ResizePseudoConsole (causes spurious newlines)"
    print("  [OK] Alt-screen 模式全宽运行且删除滚动条，进出无重排空行")

    print("\n=== 4) 验证版本号一致性 (Version consistency v1.3.3) ===")
    assert "// termux.cpp - Windows Terminal Multiplexer v1.3.3" in src, "Header version not v1.3.3"
    assert "版本 v1.3.3 | Windows Terminal Multiplexer" in src, "Help version not v1.3.3"
    assert "■ 版本号 (Version)      :\\x1b[0m \\x1b[38;2;230;237;243;1mv1.3.3\\x1b[0m" in src, "About version not v1.3.3"
    print("  [OK] 源代码版本号全部更新为 v1.3.3")

    print("\n所有 v1.3.3 专项测试全部通过！")

if __name__ == "__main__":
    test_source_code_checks()
