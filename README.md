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
| `Ctrl+B c` | 新建默认 pane |
| `Ctrl+B s` | 打开图形化设置页面 (`termux.ini`) |
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
| 点击 `[+]` | 新建 pane（选已配置项目 / 自定义命令行） |
| 点击 `[*]` | 打开图形化设置页面 |
| 点击 `termux` | 打开 / 关闭帮助 |
| 滚轮 | 滚动历史（未开鼠标追踪时） |

## 编译

```bat
:: MSVC
cl /O2 termux.cpp /link user32.lib

:: MinGW-w64
x86_64-w64-mingw32-gcc -O2 -Wall -x c -o termux.exe termux.cpp -luser32
```

> 注意：`-x c` 必须保留 —— 文件扩展名是 `.cpp`，但代码是纯 C，
> 不加 `-x c` 会让 gcc 按 C++ 编译。

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

各版本的详细更新日志与演进历史请参见：👉 **[history.md](history.md)**

- **v1.2.9**（当前版本）
  - **未截断项目悬停预览抑制**：仅当文字实际超出阈值并截断显示为 `...` 时触发悬停浮层预览，未截断的项目不再弹出冗余预览。
  - **嵌套运行（Termux 套 Termux）滚动条隔离与颜色防污染**：自动识别子层 termux 实例并抑制内部滚动条，滚动条与进度指示器全面引入 `\x1b[0;...m` / `\x1b[0m` ANSI 属性隔离，彻底杜绝色彩污染。
- 完整版本演进历史（v1.0.0 ~ v1.2.9）请查阅 **[history.md](history.md)**。

## 开源协议

本项目基于 [MIT 许可证](LICENSE) 开源。
