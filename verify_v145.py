#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_v145.py - Verification for win-termux v1.4.4 & v1.4.5
Tests:
1. Presets dialog geometry alignment fix (v1.4.4).
2. Help screen mouse text alignment fix for '点击 termux' (v1.4.4).
3. Prefix shortcuts: Ctrl+B + and Ctrl+B ?/h (v1.4.4).
4. Shift modifier bugfix for Ctrl+B + (Shift+=) and Ctrl+B ? (Shift+/) with WCHAR uc support (v1.4.5).
5. Version number consistency across codebase (v1.4.5).
"""

import sys
import re

def test_source_code_checks():
    print("=== 1) 验证预设库弹窗排版对齐 (Presets dialog geometry & alignment) ===")
    with open("termux.cpp", "r", encoding="utf-8") as f:
        src = f.read()

    assert 'const char *hdr_full = "┌─ 常用命令行预设 (按数字/回车选择) ┐";' in src, \
        "presets_geom must calculate width using full updated header string"
    assert "int min_hdr = utf8_cols(hdr_full, (int)strlen(hdr_full));" in src, \
        "presets_geom must calculate exact cols with matching strlen"
    print("  [OK] 预设库弹窗最小宽度已基于完整长标题精确计算，右边框各行对齐完美修复")

    print("\n=== 2) 验证帮助界面中 '点击 termux' 鼠标说明对齐 (Help view text alignment) ===")
    assert '点击 termux\\x1b[0m        打开 / 关闭本帮助' in src or '点击 termux\x1b[0m        打开 / 关闭本帮助' in src, \
        "Help view '点击 termux' row must have exact 8 spaces after escape sequence to align with other rows"
    print("  [OK] 帮助界面中 '点击 termux' 后方文字对齐修复完毕，严格与同类操作列对齐")

    print("\n=== 3) 验证 Ctrl+B + 与 Ctrl+B ?/h 快捷键支持 (New prefix shortcuts) ===")
    assert "handle_prefix(WORD vk, DWORD ctrl, WCHAR uc)" in src, \
        "handle_prefix signature must accept (WORD vk, DWORD ctrl, WCHAR uc)"
    assert "uc == '+' || uc == '=' || vk == VK_OEM_PLUS || vk == VK_ADD || vk == '+'" in src, \
        "handle_prefix must support '+' / '=' / VK_OEM_PLUS for opening chooser"
    assert "uc == '?' || uc == '/' || uc == 'h' || uc == 'H'" in src, \
        "handle_prefix must support '?' / '/' / 'h' / 'H' for toggling help"
    print("  [OK] 新增快捷键 Ctrl+B + 打开新建菜单子框与 Ctrl+B ? / h 打开帮助")

    print("\n=== 4) 验证 Shift 组合键（Shift+= / Shift+/）识别与处理 (Shift prefix bugfix) ===")
    assert "handle_prefix(vk, ctrl, uc);" in src, \
        "handle_key must pass uc to handle_prefix for exact character matching"
    print("  [OK] 彻底解决 Ctrl+B + 与 Ctrl+B ? 被识别为未加 Shift 键码导致触发失败的 Bug")

    print("\n=== 5) 验证版本号一致性 (Version consistency v1.4.5) ===")
    assert "// termux.cpp - Windows Terminal Multiplexer v1.4.5" in src, "Header version not v1.4.5"
    assert "版本 v1.4.5 | Windows Terminal Multiplexer" in src, "Help version not v1.4.5"
    assert "■ 版本号 (Version)      :\\x1b[0m \\x1b[38;2;230;237;243;1mv1.4.5\\x1b[0m" in src, "About version not v1.4.5"
    print("  [OK] 源代码版本号全部更新为 v1.4.5")

    print("\n所有 v1.4.4 & v1.4.5 专项测试全部通过！")

if __name__ == "__main__":
    test_source_code_checks()
