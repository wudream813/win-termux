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

- **v1.1.2** (Emoji 与字素簇全面优化)
  - **彻底解决 Emoji 单次回退删除（Backspace）与向前删除（Delete）**：在终端内部与内置弹窗中全面升级 Unicode UAX #29 字素簇（Grapheme Cluster）引擎，并引入会话级多码元原子回退机制，确保 `cmd.exe` 与 `PowerShell` 下复杂 Emoji（如 `✔️`、`😀`、`👍🏽`、`1️⃣`、`👨‍💻`）只需按 1 次 Backspace 即可完整干净删除
  - **支持光标 $\leftarrow / \rightarrow$ 跨越 Emoji 导航**：在子终端命令行中移动光标时自动感知 Emoji 码元总数，实现整字符跨越，消除光标停留在 Emoji 中间断裂的问题
  - **支持 SMP 全平面 Emoji（如 💯, 😀, 🎉, 💻, 🤖 等）无损渲染与重组**：虚拟屏幕支持高低代理对（Surrogate Pair）存储与还原，彻底解决 SMP 字符渲染为 `??` 或菱形问号 `` 的问题
  - **消除 8-bit C1 控制码误判与吞行故障**：彻底移除 UTF-8 流对过时 8-bit C1 控制字节的误触发，杜绝 Emoji 输出与系统 OSC 提示导致的解析截断和双重滚屏吞行
  - **优化嵌套运行体验**：初始化启用标准 SGR 鼠标全向事件捕获，修复 termux 嵌套运行时子 termux 鼠标 hover 响应及最右侧 `[*]` 边缘渲染
- **v1.1.1** (重大功能发布)
  - **新增图形化交互式 TUI 设置面板 (`[*]` / `Ctrl+B s`)**：内置可视化配置编辑器，支持鼠标与键盘全套快捷操作：`Ctrl+↑/↓` 切换选择项、`↑/↓` 直接调整项目排序、`Enter` 编辑条目、`Ctrl+D` 删除条目，右侧操作按钮（`[↑][↓][改][删]`）支持实时鼠标悬停高亮（Hover）
  - **支持快捷预设加载**：预设面板提供 cmd、PowerShell、WSL Ubuntu、Git Bash、Python、Node.js 及自定义命令行等常用开发环境一键填充
  - **新增 `termux.ini` 配置文件持久化**：支持自定义新建 Pane 菜单的条目数量（1~9 项）、显示名称、排列顺序与启动命令行，自动生成模板，设置修改即时同步保存
  - **优化终端字符对齐与图标兼容**：将齿轮图标更换为标准 ASCII `[*]` 符号，重构全角/半角/中文字符列宽排版算法，彻底消除界面错位问题
- **v1.0.5**
  - 彻底修复 `termux` 嵌套运行（termux 套 termux）时，鼠标移入外层 Tab 栏导致子 termux 标签悬停（Hover）残留卡住的问题
- **v1.0.4**
  - 更改标题与自定义命令行输入框全面支持 `←` / `→` / `Home` / `End` / `Delete` 任意位置光标导航与编辑
  - 引入字素簇（Grapheme Cluster）算法，支持复杂 Emoji（如 `✍️`、ZWJ连字序列、变体选择符等）一键 Backspace 完整删除
  - 修复光标位置异常停留在状态栏（Tab Bar）末尾的渲染顺序问题
  - 修复自定义命令行输入框右侧边框对齐问题（校准宽度）
  - 统一编译输出与发布资产名为 `termux.exe`，精简 MinGW 编译参数（移除 `-Wextra`）
- **v1.0.3**
  - 优化新建弹窗切换渲染：自定义命令行输入框完整覆盖旧新建弹窗，彻底消除残余边框与文字
  - 新增进程异常退出保持机制：命令执行报错（退出码非 0）或启动失败时，完整保留错误日志并显示红色提示，按任意键或点击关闭才退出
- **v1.0.2**
  - 新增在新建 Pane 弹窗中支持 `[3]` 自定义命令行启动（可输入指定命令如 `wsl`、`bash`、`python`、`pwsh` 等运行）
  - 新增终端窗口标题自动同步当前活动子 Pane 标题（支持 OSC 标题及改名实时同步，退出自动恢复原标题）
  - 修复 Windows cmd / ConPTY `title` 命令产生前缀冒号（`:   T`）的解析问题
- **v1.0.1**
  - 修复终端窗口调整大小（Resize）导致历史滚动记录丢失被清空的问题
  - 修复帮助页面中在标签栏滑动鼠标导致全屏剧烈闪烁的问题（移除 2J 屏幕清除）
  - 修复 Pane 区域鼠标点击与滚轮事件 Y 坐标整体下偏 1 行（未扣除 Tab Bar 高度）的问题
  - 修复 Pane 双击（`DOUBLE_CLICK`）事件在子程序中被误丢弃的问题
  - 解耦 SGR `1006` 与跟踪模式 `1000/1002/1003`，修复 SGR 模式下移动事件泛滥及模式覆盖问题
  - 修复 SGR 模式下按键释放（Button Up）的按键识别，精准区分左/中/右键释放
  - 增加控制台必须使用等宽字体的警告提示
  - 消除 MinGW 编译警告，优化渲染热路径与资源管理
- **v1.0.0** 首个正式发布版
  - 修复右键内容区误弹菜单（移除非标签页右键兜底）
  - 启动提速（移除启动横幅 800ms 人为延迟）
  - 8 号粉色调色板修复（修复 `color & 7` 索引折叠）
  - 鼠标全量诊断日志支持（`TERMUX_DUMP=1`）

## 开源协议

本项目基于 [MIT 许可证](LICENSE) 开源。
