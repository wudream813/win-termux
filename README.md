# win-termux

Windows 终端复用器（Terminal Multiplexer）—— 单文件 C 实现，基于 Windows ConPTY。
在 Windows 控制台里管理多个 cmd / PowerShell 会话，像 tmux 一样分标签页。

## 特性

- **多标签页**：每个标签一个独立的 cmd / PowerShell 会话（ConPTY 后端，需 Win10 1809+）
- **GitHub-Dark 风格 UI**：真彩色（24bit）渲染，中文界面
- **标签页染色**：8 色调色板，右键标签 → 改颜色 / 改标题，或 `Ctrl+B t` 轮换
- **鼠标支持**：点击/中键/右键标签操作、滚轮滚动、hover 高亮
- **滚动历史**：10000 行环形滚动缓冲，PgUp/PgDn/滚轮查看
- **Alt 屏幕支持**：nano / vim 等全屏编辑器正常使用，退出后历史完整保留
- **内置帮助**：点击左上角 `termux` 徽标查看
- **诊断日志**：`TERMUX_DUMP=1` 时输出原始 ConPTY 流 / 渲染输出 / 鼠标事件

## 快捷键

| 快捷键 | 功能 |
|---|---|
| `Ctrl+B c` | 新建 cmd pane |
| `Ctrl+B n / p` | 下一个 / 上一个 pane |
| `Ctrl+B x` | 关闭当前 pane |
| `Ctrl+B d` | 退出 termux |
| `Ctrl+B t` / `Shift+t` | 轮换标签颜色 |
| `Ctrl+B 0-9` | 跳转到 pane |

## 鼠标操作

| 操作 | 功能 |
|---|---|
| 左键点击标签 | 切换 pane |
| 左键点击 `×` | 关闭该 pane |
| 右键点击标签 | 改颜色 / 改标题 |
| 中键点击标签 | 关闭该 pane |
| 点击 `[+]` | 新建 pane（选 cmd / PowerShell） |
| 点击 `termux` | 打开 / 关闭帮助 |
| 滚轮 | 滚动历史（未开鼠标追踪时） |

## 编译

```bat
:: MSVC
cl /O2 termux.cpp /link user32.lib

:: MinGW-w64
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -x c -o termux-v1.0.0.exe termux.cpp -luser32
```

> 注意：`-x c` 必须保留 —— 文件扩展名是 `.cpp`，但代码是纯 C，
> 不加 `-x c` 会让 gcc 按 C++ 编译。

## 运行

```bat
termux-v1.0.0.exe          :: 正常启动
set TERMUX_DUMP=1   :: 启用诊断日志（termux_dump.log / render_dump.log / mouse_dump.log）
termux-v1.0.0.exe
```

## 系统要求

- Windows 10 1809 (RS5) 或更高（ConPTY 支持）
- 从真实控制台窗口运行（cmd / Windows Terminal / ConEmu 等）

## 开发说明

源码是**单文件 C**（`termux.cpp`），无第三方依赖，仅链接 `user32`。
每次改动使用仓库内的 Python 验证脚本回归：

```bash
python3 verify_picker.py    # 选色器几何 / 点击 / hover 命中
python3 verify_flow.py      # 弹窗端到端流程
python3 verify_mouse53.py   # 鼠标按钮优先级 / 兜底 / 渲染用色
python3 verify_color8.py    # color=8 渲染用色回归
```

## 版本历史

- **v1.0.0** 首个正式发布版（原 v8.57）。修复右键内容区误弹菜单；启动提速；8 号粉色调色板修复；鼠标全量诊断日志
- **v8.56** 启动提速：移除启动横幅 800ms 人为延迟
- **v8.55** 修复 `color & 7` 折叠 bug —— 8 号（粉色）现在正确渲染
- **v8.54** 鼠标事件全量日志（`mouse_dump.log`）+ 启动版本标记；键盘选色 vk 兜底
- **v8.53** mbtn 判断改为 左键 > 右键 > 中键（解决中键粘连吞右键）；tab bar 空白 / pane 右键兜底
- **v8.52** 修复选色器点击被嵌套在 chooser 分支内导致完全失效；弹窗内鼠标移动实时重绘 hover
- **v8.51** 调色板 hover 高亮、8 号色块可点击区域、中文输入右边界对齐（utf8_cols）
