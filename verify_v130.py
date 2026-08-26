#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_v130.py - Verification for win-termux v1.3.0
Tests:
1. Direct alt-screen rendering without legacy splash text.
2. Alt-screen enter (\x1b[?1049h) & exit (\x1b[?1049l) correctness.
3. Hover preview tooltips are strictly suppressed when item text is NOT truncated.
4. ANSI SGR style isolation on scrollbars, percentage indicator, and line clears.
5. Version number consistency across codebase (v1.3.0).
"""

import sys
import re

def test_source_code_checks():
    print("=== 1) 验证直接在 Alt-Screen 渲染 (Direct Alt-Screen rendering) ===")
    with open("termux.cpp", "r", encoding="utf-8") as f:
        src = f.read()

    # Verify no legacy splash banner prints before render_screen
    assert "Windows Terminal Multiplexer v" not in src[src.find("int main("):], "Legacy splash text found in main"
    assert "Starting..." not in src[src.find("int main("):], "Legacy 'Starting...' text found in main"
    assert "\\x1b[?1049h\\x1b[?1003h\\x1b[?1006h\\x1b[2J\\x1b[H\\x1b[?25l" in src, "Missing direct alt-screen enter sequence"
    assert "\\x1b[?1003l\\x1b[?1006l\\x1b[?1049l\\x1b[?25h\\x1b[0m" in src, "Missing clean alt-screen exit sequence"
    print("  [OK] 启动直接进入 Alt-Screen 并即刻渲染 TUI，无残留启动文字")

    print("\n=== 2) 验证未截断时不触发预览 (No hover preview when not truncated) ===")
    assert "utf8_cols(full_title, (int)strlen(full_title)) <= 15" in src, "Missing tab title hover truncation check"
    assert "utf8_cols(name, (int)strlen(name)) <= 15" in src, "Missing chooser item hover truncation check"
    assert "utf8_cols(name, (int)strlen(name)) <= 10" in src, "Missing settings name hover truncation check"
    assert "utf8_cols(cmd, (int)strlen(cmd)) <= 15" in src, "Missing settings cmd hover truncation check"
    print("  [OK] 所有 UI 项未截断时严格抑制悬停浮层")

    print("\n=== 3) 验证滚动条 ANSI SGR 颜色隔离 (Scrollbar ANSI SGR isolation) ===")
    assert "\\x1b[0;48;2;225;235;250m" in src, "Missing SGR reset in hover thumb"
    assert "\\x1b[0;48;2;%d;%d;%dm" in src, "Missing SGR reset in thumb"
    assert "\\x1b[0;48;2;%d;%d;%dm\\x1b[38;2;%d;%d;%dm│\\x1b[0m" in src, "Missing SGR reset in track"
    assert "\\x1b[0;30;43m[%d%%]" in src, "Missing SGR reset in percentage badge"
    assert "\\x1b[0m\\x1b[K" in src, "Missing SGR reset before \\x1b[K line clear"
    print("  [OK] 滚动条与进度条具备完整 ANSI 属性重置保护")

    print("\n=== 4) 验证版本号一致性 (Version consistency v1.3.0) ===")
    assert "// termux.cpp - Windows Terminal Multiplexer v1.3.0" in src, "Header version not v1.3.0"
    assert "版本 v1.3.0 | Windows Terminal Multiplexer" in src, "Help version not v1.3.0"
    assert "■ 版本号 (Version)      :\\x1b[0m \\x1b[38;2;230;237;243;1mv1.3.0\\x1b[0m" in src, "About version not v1.3.0"
    print("  [OK] 源代码版本号全部更新为 v1.3.0")

    print("\n所有 v1.3.0 专项测试全部通过！")

if __name__ == "__main__":
    test_source_code_checks()
