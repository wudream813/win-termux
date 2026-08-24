# win-termux

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Windows 终端复用器（Terminal Multiplexer）—— 单文件 C 实现，基于 Windows ConPTY。
在 Windows 控制台里管理多个 cmd / PowerShell 会话，像 tmux 一样分标签页。

> ⚠️ **警告 / 注意事项**：
> 控制台终端**必须配置使用等宽字体**（Monospace Font，例如 *Cascadia Code*、*Consolas*、*JetBrains Mono*、*Fira Code* 等）。
> 使用非等宽字体会导致字符宽度计算偏差、边框排版错位及界面渲染故障。

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
| 点击 `[+]` | 新建 pane（选 cmd / PowerShell / 自定义命令） |
| 点击 `termux` | 打开 / 关闭帮助 |
| 滚轮 | 滚动历史（未开鼠标追踪时） |

## 编译

```bat
:: MSVC
cl /O2 termux.cpp /link user32.lib

:: MinGW-w64
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -x c -o termux-v1.0.1.exe termux.cpp -luser32
```

> 注意：`-x c` 必须保留 —— 文件扩展名是 `.cpp`，但代码是纯 C，
> 不加 `-x c` 会让 gcc 按 C++ 编译。

## 运行

```bat
termux-v1.0.1.exe          :: 正常启动
set TERMUX_DUMP=1   :: 启用诊断日志（termux_dump.log / render_dump.log / mouse_dump.log）
termux-v1.0.1.exe
```

## 系统要求

- Windows 10 1809 (RS5) 或更高（ConPTY 支持）
- 从真实控制台窗口运行（cmd / Windows Terminal / ConEmu 等）
- **必须使用等宽字体**（如 Cascadia Code, Consolas 等），否则会出现渲染故障与排版错位

## 开发说明

源码是**单文件 C**（`termux.cpp`），无第三方依赖，仅链接 `user32`。
每次改动可一键运行仓库内的全部 Python 回归验证：

```bash
python3 verify_all.py
```

也可以单独运行验证脚本：

```bash
python3 verify_picker.py    # 选色器几何 / 点击 / hover 命中
python3 verify_flow.py      # 弹窗端到端流程
python3 verify_mouse53.py   # 鼠标按钮优先级 / 兜底 / 渲染用色
python3 verify_color8.py    # color=8 渲染用色回归
```


## 版本历史

- **v1.0.1**
  - 新增在新建 Pane 弹窗中支持 `[3]` 自定义命令行启动（可输入指定命令如 `wsl`、`bash`、`python`、`pwsh` 等运行）
  - 新增终端窗口标题自动同步当前活动子 Pane 标题（支持 OSC 标题及改名实时同步，退出自动恢复原标题）
  - 修复 Windows cmd / ConPTY `title` 命令产生前缀冒号（`:   T`）的解析问题
  - 修复终端窗口调整大小（Resize）导致历史滚动记录丢失被清空的问题
  - 修复帮助页面中在标签栏滑动鼠标导致全屏剧烈闪烁的问题
  - 修复 Pane 区域鼠标点击与滚轮事件 Y 坐标整体下偏 1 行（未扣除 Tab Bar 高度）的问题
  - 修复 Pane 双击（`DOUBLE_CLICK`）事件在子程序中被误丢弃的问题
  - 解耦 SGR `1006` 与跟踪模式 `1000/1002/1003`，修复 SGR 模式下移动事件泛滥及模式覆盖问题
  - 修复 SGR 模式下按键释放（Button Up）的按键识别，精准区分左/中/右键释放
  - 消除 MinGW 编译警告，优化渲染热路径与资源管理
- **v1.0.0** 首个正式发布版
  - 修复右键内容区误弹菜单（移除非标签页右键兜底）
  - 启动提速（移除启动横幅 800ms 人为延迟）
  - 8 号粉色调色板修复（修复 `color & 7` 索引折叠）
  - 鼠标全量诊断日志支持（`TERMUX_DUMP=1`）

## 开源协议

本项目基于 [MIT 许可证](LICENSE) 开源。
