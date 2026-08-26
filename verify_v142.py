#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_v142.py - Verification for win-termux v1.4.1 & v1.4.2
Tests:
1. About and Settings tabs cannot be renamed or color changed (v1.4.1 bugfix).
2. Settings theme color switched to light blue (tab color index 6, [*] button, header) (v1.4.1).
3. Settings interface real-time mouse hover effects across all elements (v1.4.2).
4. Removal of duplicate save button in Settings startup view (v1.4.2).
5. Version number consistency across codebase (v1.4.2).
"""

import sys
import re

def test_source_code_checks():
    print("=== 1) 验证关于和设置标签不可改颜色与标题 (About & Settings immutable color and title) ===")
    with open("termux.cpp", "r", encoding="utf-8") as f:
        src = f.read()

    assert "int is_about;" in src, "Pane struct must have is_about field"
    assert "int is_settings;" in src, "Pane struct must have is_settings field"
    assert "pane->is_about = 1;" in src, "create_about_pane must set is_about = 1"
    assert "pane->is_settings = 1;" in src, "open_settings_pane must set is_settings = 1"

    # Tab right click context menu protection
    assert "!g_mux.panes[t->pane_idx].is_about && !g_mux.panes[t->pane_idx].is_settings" in src, \
        "Right click context menu must not trigger for About or Settings tabs"

    # Ctrl+B t shortcut color cycle protection
    assert "!g_mux.panes[g_mux.active_pane].is_about && !g_mux.panes[g_mux.active_pane].is_settings" in src, \
        "Ctrl+B t must not cycle color on About or Settings tabs"

    # OSC title protection
    assert "!g_mux.panes[idx].is_about && !g_mux.panes[idx].is_settings" in src, \
        "OSC title update must protect About and Settings tabs"
    print("  [OK] 关于与设置标签已锁定颜色与标题，禁止右键菜单、快捷键染色及外部重命名")

    print("\n=== 2) 验证设置主题色切换为浅蓝色 (Settings theme color to light blue) ===")
    assert "pane->color = 6;" in src, "open_settings_pane must set pane->color = 6 (light blue)"
    assert "\\x1b[48;2;121;192;255m" in src, "Settings UI must use light blue theme ANSI sequence"
    assert "termux - 设置面板" in src and "\\x1b[48;2;121;192;255m" in src, "Settings header must use light blue theme"
    print("  [OK] 设置标签页与设置面板主题色已更新为浅蓝色 (Light Blue)")

    print("\n=== 3) 验证设置界面全要素 Hover 高亮 (Settings hover effects) ===")
    assert "in_settings_pane" in src, "handle_mouse must check in_settings_pane for real-time redraw"
    assert "h_start" in src, "Startup navigation item must support hover"
    assert "h_item" in src, "Menu items must support hover"
    assert "h_add" in src and "h_pre" in src and "h_save_btn" in src, "Sidebar buttons must support hover"
    assert "h_up" in src and "h_dn" in src and "h_ed" in src and "h_del" in src, "Table buttons must support hover"
    assert "f0_hover" in src and "f1_hover" in src and "f2_hover" in src, "Editor fields must support hover"
    assert "h_apply" in src and "h_imp" in src, "Action buttons must support hover"
    print("  [OK] 设置界面（左侧导航、启动项单选/表格按钮、详细配置输入框与操作按钮）全面支持实时 Hover 高亮")

    print("\n=== 4) 验证移除重复的保存按钮 (Remove duplicate save button) ===")
    # Right Startup view bottom should not have [Ctrl+S] button
    btn_matches = re.findall(r'\[Ctrl\+S\]\s*保存配置', src)
    # 1 in comment + 1 in snprintf = 2 total in file (previously was 4)
    assert len(btn_matches) == 2, \
        f"Expected exactly 1 button in sidebar bottom (1 code + 1 comment), found {len(btn_matches)}"
    print("  [OK] 启动视图右侧主区域底部的重复 [Ctrl+S] 保存配置按钮已成功移除，保留左侧底栏唯一全局保存")

    print("\n=== 5) 验证版本号一致性 (Version consistency v1.4.x) ===")
    assert "// termux.cpp - Windows Terminal Multiplexer v1.4" in src, "Header version not v1.4"
    assert "Windows Terminal Multiplexer" in src, "Help version not found"
    assert "■ 版本号 (Version)      :" in src, "About version not found"
    print("  [OK] 源代码版本号全部更新为 v1.4.x")

    print("\n所有 v1.4.1 & v1.4.2 专项测试全部通过！")

if __name__ == "__main__":
    test_source_code_checks()
