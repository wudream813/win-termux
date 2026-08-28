#!/usr/bin/env python3
"""配置体系（[general] / [theme] / [keys]）的源码级回归验证。

分两部分：

1. 源码不变量 —— 主题引擎依赖「UI 配色一律写成零填充形式」这一约定，
   任何新加的界面颜色如果忘了补零、或者忘了登记进 theme.c 的参考色板，
   换主题时就会变成一块不跟随主题的死色。这里把两个方向都卡死。

2. C 单元测试 —— tests/test_config.c 用 tests/stub 里的 windows.h 替身在
   本机原生编译执行，直接断言 theme.c / keymap.c 的真实行为（而不是用
   Python 再复刻一遍逻辑）。
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
UI_SOURCES = ("src/render.c", "src/utf8.c", "src/pane.c")

failures: list[str] = []


def fail(msg: str) -> None:
    failures.append(msg)
    print(f"  [FAIL] {msg}")


def ok(msg: str) -> None:
    print(f"  [ok] {msg}")


# --------------------------------------------------------------------------
# 1. 源码不变量
# --------------------------------------------------------------------------

PADDED = re.compile(r"0(?:38|48);2;(\d{3});(\d{3});(\d{3})")
# 静态字面量里的颜色（%d 动态拼装的 pane 内容色不在此列）
UNPADDED = re.compile(r"(?<![0-9%])(?:38|48);2;(\d{1,3});(\d{1,3});(\d{1,3})")


def collect_ui_colors() -> set[tuple[int, int, int]]:
    colors: set[tuple[int, int, int]] = set()
    for rel in UI_SOURCES:
        text = (ROOT / rel).read_text(encoding="utf-8")
        for m in PADDED.finditer(text):
            colors.add(tuple(int(g) for g in m.groups()))  # type: ignore[arg-type]
        for m in UNPADDED.finditer(text):
            line = text[: m.start()].count("\n") + 1
            fail(f"{rel}:{line} 界面颜色未使用零填充形式: {m.group(0)}")
    return colors


def parse_theme_refs() -> tuple[set[tuple[int, int, int]], list[str], dict[str, list[tuple[int, int, int]]]]:
    text = (ROOT / "src/theme.c").read_text(encoding="utf-8")
    block = text.split("static const ThemeRef g_theme_refs[] = {", 1)[1].split("};", 1)[0]
    refs: set[tuple[int, int, int]] = set()
    for line in block.splitlines():
        m = re.match(r"\s*\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(TH_\w+),\s*(TH_\w+),\s*(\d+)\s*\}", line)
        if m:
            refs.add((int(m.group(1)), int(m.group(2)), int(m.group(3))))

    roles = re.search(r"static const char \*const g_role_names\[TH_ROLE_COUNT\] = \{(.*?)\};", text, re.S)
    role_names = re.findall(r'"([a-z_]+)"', roles.group(1)) if roles else []

    themes: dict[str, list[tuple[int, int, int]]] = {}
    tblock = text.split("const ThemeDef g_builtin_themes[] = {", 1)[1].split("\n};", 1)[0]
    for m in re.finditer(r'\{"([\w-]+)",\s*\{(.*?)\}\},', tblock, re.S):
        nums = [int(x) for x in re.findall(r"C\(\s*(\d+),\s*(\d+),\s*(\d+)\)", m.group(2)) for x in x]
        triples = [tuple(nums[i:i + 3]) for i in range(0, len(nums), 3)]
        themes[m.group(1)] = triples  # type: ignore[assignment]
    return refs, role_names, themes


def check_sources() -> None:
    print("== 源码不变量 ==")
    ui_colors = collect_ui_colors()
    refs, role_names, themes = parse_theme_refs()

    missing = sorted(ui_colors - refs)
    for c in missing:
        fail("界面用色未登记到 theme.c 的参考色板: %d;%d;%d" % c)
    if not missing:
        ok(f"{len(ui_colors)} 个界面用色全部登记在参考色板中")

    unused = sorted(refs - ui_colors)
    for c in unused:
        fail("参考色板中存在界面里已不再使用的死色: %d;%d;%d" % c)
    if not unused:
        ok("参考色板没有多余条目")

    if len(role_names) != 16:
        fail(f"语义角色数量异常: {len(role_names)}")
    else:
        ok(f"16 个语义角色: {', '.join(role_names)}")

    for name, triples in themes.items():
        if len(triples) != len(role_names):
            fail(f"主题 {name} 的角色色数量为 {len(triples)}，应为 {len(role_names)}")
    if themes:
        ok(f"内置主题: {', '.join(themes)}")
    if "github-dark" not in themes:
        fail("默认主题 github-dark 缺失")

    # README 必须记录每个可配置项与动作名
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    for key in ("theme", "prefix", "scrollback", "mouse", "copy_on_select", "confirm_on_exit"):
        if key not in readme:
            fail(f"README 未记录 [general] 配置项: {key}")
    actions = re.findall(r'\{ACT_\w+,\s*"([a-z-]+)"', (ROOT / "src/keymap.c").read_text(encoding="utf-8"))
    for act in actions:
        if act not in readme:
            fail(f"README 未记录动作名: {act}")
    if actions:
        ok(f"{len(actions)} 个动作名均已在 README 中记录")
    for role in role_names:
        if role not in readme:
            fail(f"README 未记录主题角色: {role}")

    # 配置读写必须覆盖四个段
    config_c = (ROOT / "src/config.c").read_text(encoding="utf-8")
    for section in ("general", "menu", "theme", "keys"):
        if f'"{section}"' not in config_c:
            fail(f"config.c 未解析 [{section}] 段")
        if f"[{section}]" not in config_c:
            fail(f"config.c 未写出 [{section}] 段")
    ok("config.c 读写覆盖 [general] / [theme] / [keys] / [menu]")

    # 保存时写出的主题名注释应与内置主题一致
    for name in themes:
        if name not in config_c:
            fail(f"config.c 生成的注释未列出主题 {name}")


# --------------------------------------------------------------------------
# 2. C 单元测试
# --------------------------------------------------------------------------

def check_unit_tests() -> None:
    print("\n== tests/test_config.c 单元测试 ==")
    cc = shutil.which("gcc") or shutil.which("cc") or shutil.which("clang")
    if not cc:
        print("  [skip] 未找到 C 编译器，跳过原生单元测试")
        return
    out = ROOT / ".test_config.bin"
    cmd = [cc, "-O1", "-Wall", "-Wextra", "-Werror",
           "-Itests/stub", "-Iinclude",
           "src/theme.c", "src/keymap.c", "tests/test_config.c", "-o", str(out), "-lm"]
    build = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if build.returncode:
        fail("单元测试编译失败:\n" + build.stdout + build.stderr)
        return
    run = subprocess.run([str(out)], cwd=ROOT, capture_output=True, text=True)
    print("\n".join("  " + line for line in run.stdout.strip().splitlines()))
    if run.returncode:
        fail("tests/test_config.c 存在失败断言")
    out.unlink(missing_ok=True)


def main() -> int:
    check_sources()
    check_unit_tests()
    if failures:
        print(f"\n配置体系验证失败：{len(failures)} 项", file=sys.stderr)
        return 1
    print("\n配置体系验证通过。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
