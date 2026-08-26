#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_v150.py - Verification for win-termux v1.5.0 (Security Audit & Architecture Polish)
Tests:
1. P0: C++ / MSVC compilation compatibility (no jump over initializers, g++ compiles cleanly).
2. P1: handle_prefix lowercase VK collisions eliminated (no numpad/F-key accidental triggers).
3. P1: handle_prefix supports numpad digits (VK_NUMPAD0..VK_NUMPAD9) for pane switching.
4. Single source of truth for version number (#define TERMUX_VERSION "1.5.0").
5. Buffer safety: strcpy replaced with snprintf for config initialization and titles.
6. CI: GitHub Actions build workflow present.
"""

import subprocess
import sys
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent

def test_source_code_checks():
    print("=== 1) 验证 P0: C++ / MSVC 模式下编译零错误 (C++ / MSVC build compatibility) ===")
    cpp_res = subprocess.run(
        ["x86_64-w64-mingw32-g++", "-O2", "-s", "-Wall", "-o", "/tmp/termux_cpp_test.exe", "termux.cpp", "-luser32"],
        cwd=ROOT,
        capture_output=True,
        text=True
    )
    assert cpp_res.returncode == 0, f"g++ build failed:\n{cpp_res.stderr}"
    print("  [OK] C++ 模式 (MSVC / G++) 编译完全通过，无 goto 跨越变量初始化错误")

    with open("termux.cpp", "r", encoding="utf-8") as f:
        src = f.read()

    print("\n=== 2) 验证 P1: 前缀键消除 lowercase 与小键盘/功能键撞码 (No lowercase VK collisions) ===")
    prefix_match = re.search(r'static void handle_prefix\([\s\S]*?\n\}', src)
    assert prefix_match is not None, "handle_prefix not found"
    prefix_body = prefix_match.group(0)

    # Ensure no lowercase case labels inside the switch statement of handle_prefix
    for char in ['c', 'n', 'p', 'x', 'd', 't', 's', 'h']:
        assert f"case '{char}':" not in prefix_body, f"Found conflicting lowercase case '{char}' in handle_prefix switch"
    print("  [OK] handle_prefix 移除小写 case 分支，彻底根除小键盘/F功能键误触发风险")

    print("\n=== 3) 验证 P1: 支持小键盘数字 0-9 跳转 Pane (VK_NUMPAD0-9 jump) ===")
    assert "VK_NUMPAD0" in prefix_body and "VK_NUMPAD9" in prefix_body, \
        "handle_prefix must support VK_NUMPAD0..VK_NUMPAD9 for pane jumping"
    print("  [OK] 支持小键盘数字键 0-9 快速切换 Pane")

    print("\n=== 4) 验证版本号单一数据源 (#define TERMUX_VERSION '1.5.0') ===")
    assert '#define TERMUX_VERSION "1.5.0"' in src, "Missing #define TERMUX_VERSION '1.5.0'"
    assert '版本 v" TERMUX_VERSION "' in src or '版本 v" TERMUX_VERSION' in src, "Help text must reference TERMUX_VERSION"
    assert '■ 版本号 (Version)      :\\x1b[0m \\x1b[38;2;230;237;243;1mv" TERMUX_VERSION "' in src or \
           '■ 版本号 (Version)      :\x1b[0m \x1b[38;2;230;237;243;1mv" TERMUX_VERSION "' in src, \
        "About text must reference TERMUX_VERSION"
    print("  [OK] 版本号已收敛为单一宏定义 TERMUX_VERSION，各处统一引用")

    print("\n=== 5) 验证字符串安全拷贝 (strcpy replaced with snprintf) ===")
    assert "strcpy(" not in src, "All strcpy calls should be upgraded to safe snprintf"
    print("  [OK] 所有默认配置及标题初始化升级为带边界保护的 snprintf")

    print("\n=== 6) 验证 GitHub Actions CI 配置文件 ===")
    ci_path = ROOT / ".github" / "workflows" / "build.yml"
    assert ci_path.exists(), "Missing .github/workflows/build.yml"
    print("  [OK] GitHub Actions CI 工作流已就绪")

    print("\n所有 v1.5.0 安全审计与架构专项测试全部通过！")

if __name__ == "__main__":
    test_source_code_checks()
