# win-termux

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Windows 终端复用器（Terminal Multiplexer）—— 模块化 C 架构，基于 Windows ConPTY。
在 Windows 控制台里管理多个 cmd / PowerShell 会话，像 tmux 一样分标签页。

当前版本：**v1.8.3**

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
- **分层命令面板**：仅通过 `Ctrl+B :` 打开（同时兼容中文全角冒号 `：`）；普通终端直接进入“操作命令面板”，设置页直接进入“设置命令面板”。操作面板支持新建终端（子面板内可按名称/命令搜索）、自定义命令行、标题/颜色、历史搜索、panel 切换（按编号或标题并显示颜色）、复制模式、热重载、关闭当前 panel、退出整个 termux，并可切换到设置命令面板；设置面板包含图形化设置页面入口、操作命令面板入口、默认启动项、打开 `.ini`、添加 panel 条目（预设/自定义后继续编辑）及菜单项设置（进入子面板后可直接编辑已有条目）。
- **诊断日志**：`TERMUX_DUMP=1` 时输出原始 ConPTY 流 / 渲染输出 / 鼠标事件

## 快捷键

| 快捷键 | 功能 |
|---|---|
| `Ctrl+B :` | 打开命令面板 (Command Palette) |
| `Ctrl+B c` | 新建默认 pane |
| `Ctrl+B +` | 新建 pane 菜单（选已配置项目 / 自定义命令行） |
| `Ctrl+B [` | 进入复制模式（方向键/hjkl移动、Space/v选区、Enter/y复制、Esc退出） |
| `Ctrl+B /` | 搜索滚动历史（n/N 跳转匹配，Esc 退出） |
| `Ctrl+B s` | 打开图形化设置页面 (`termux.ini`) |
| `Ctrl+B r` | 热重载配置文件 (`termux.ini`) |
| `Ctrl+B ?` / `h` | 打开 / 关闭帮助 |
| `Ctrl+B n / p` | 下一个 / 上一个 pane |
| `Ctrl+B x` | 关闭当前 panel |
| `Ctrl+B d` | 退出 termux |
| `Ctrl+B t` / `Shift+t` | 轮换标签颜色 |
| `Ctrl+B 0-9` | 跳转到 pane（支持主键盘与小键盘数字） |

## 鼠标操作

| 操作 | 功能 |
|---|---|
| 左键点击标签 | 切换 pane |
| 左键点击 `×` | 关闭该 pane |
| 右键点击标签 | 改颜色 / 改标题 |
| 中键点击标签 | 关闭该 pane |
| 鼠标左键拖拽 | 框选终端文字，松开自动复制到剪贴板 |
| 点击 `[+]` | 新建 pane（选已配置项目 / 自定义命令行） |
| 点击 `[*]` | 打开图形化设置页面 |
| 点击 `termux` | 打开 / 关闭帮助 |
| 滚轮 | 滚动历史（未开鼠标追踪时） |

## 编译

```bat
:: MSVC
cl /O2 /Iinclude src\*.c /Fe:termux.exe /link user32.lib shell32.lib

:: MinGW-w64 (GCC / Make)
make
:: 或手动执行
x86_64-w64-mingw32-gcc -O2 -s -Wall -Wextra -Iinclude src/*.c -o termux.exe -luser32 -lshell32
```

## 运行

```bat
termux.exe          :: 正常启动
set TERMUX_DUMP=1   :: 启用诊断日志（termux_dump.log / render_dump.log / mouse_dump.log）
termux.exe
```

## ⚙️ 配置文件 (termux.ini)

`termux` 支持通过 `termux.ini` 配置文件自定义 **[+] 新建菜单** 中的条目、顺序与启动命令。

- **配置文件路径**：优先读取 `termux.exe` 所在目录下的 `termux.ini`；若不存在则读取 `%USERPROFILE%\.termux.ini`。首次启动会自动生成默认配置文件。
- **配置示例**：
  ```ini
  # win-termux 配置文件 (UTF-8)
  # 格式: 序号 = 菜单显示名称, 启动命令行
  # 特殊命令 ":custom" 表示打开自定义命令行输入框

  [menu]
  1 = cmd, cmd.exe
  2 = PowerShell, powershell.exe
  3 = 自定义命令行, :custom
  # 自定义示例 (取消注释即可启用，支持 1-9 项，按数字键或鼠标点击直接启动):
  # 4 = WSL Ubuntu, wsl.exe -d Ubuntu
  # 5 = Git Bash, "C:\Program Files\Git\bin\bash.exe" --login -i
  # 6 = Python, python -i
  ```

## 系统要求

- Windows 10 1809 (RS5) 或更高（ConPTY 支持）
- 从真实控制台窗口运行（cmd / Windows Terminal / ConEmu 等）
- **必须使用等宽字体**（如 Cascadia Code, Consolas 等），否则会出现渲染故障与排版错位

## 开发说明

源码采用模块化架构（`include/` 与 `src/`），无第三方依赖，仅链接 `user32`。
每次改动可一键运行仓库内的全部 Python 回归验证：

```bash
python3 verify_all.py
```

也可以单独运行验证脚本：

```bash
python3 verify_picker.py        # 选色器几何 / 点击 / hover 命中
python3 verify_flow.py          # 弹窗端到端流程
python3 verify_mouse53.py       # 鼠标按钮优先级 / 兜底 / 渲染用色
python3 verify_color8.py        # color=8 渲染用色回归
python3 verify_emoji.py         # Emoji 与字素簇边界测试
python3 verify_ringbuf_asan.py  # 环形缓冲区局部滚动 ASAN 内存安全
python3 verify_search.py          # 滚动历史搜索行为验证
python3 verify_palette_search.py   # 新建终端短查询/名称优先搜索回归
python3 verify_input_layout.py     # 输入框背景/末尾字符/光标列回归
```


## 版本历史

详见 [history.md](history.md)。

## 开源协议

本项目基于 [MIT 许可证](LICENSE) 开源。
