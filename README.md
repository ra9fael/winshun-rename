# 批量重命名（ra9fael.rename）

Win顺 的批量重命名插件：右键选中的文件或文件夹打开工作窗口，用规则链或文本缓冲区预览并改名。选中的项目就是窗口的输入集，选中一个文件夹改的是这个文件夹本身；文件夹里的项目从“在此文件夹批量重命名…”打开。单文件改名请用 Win顺 自带的 F2。

**0.4.5 已发布。** 源码与 Release 在 [ra9fael/winshun-rename](https://github.com/ra9fael/winshun-rename)，tag `v0.4.5` 对应 commit `046f77b`：发布工作流从该 tag 在 windows-2022 上构建、跑测试、打包，再把 `.wsplugin` 附到 Release。
语言/文档互链：[English](README.en.md) · [设计与需求](plugin-design.md) · [界面规格](UI-SPEC.md) · [测试计划](TEST-PLAN.md) · [Vim 模式](docs/VIM-MODE.md)。

## 安装

- 插件库：`Win顺 → 设置 → 插件 → 插件库`，搜“批量重命名”安装。
- 手动：在 [v0.4.5 Release](https://github.com/ra9fael/winshun-rename/releases/tag/v0.4.5) 下载 `ra9fael.rename-0.4.5.wsplugin`（同页有 `.sha256`）后安装。

需要 Win顺 0.33.0 或更高：右键菜单的显示开关用的是 `triggers.files.if`，该版本起宿主才会读它。包内带 `Qt6Widgets.dll`：Win顺 的共享 Qt 契约还没有 Qt Widgets，Qt 6.12 的其余部分由宿主提供。

## 入口

- 右键：选中文件或文件夹，结果菜单里的“批量重命名…”打开窗口，窗口的输入集就是选中的这些项目。插件卡片上的“在文件结果右键菜单显示重命名”（默认开）只控制这一项；关掉后关键词入口照常，插件也不停用。
- 当前文件夹：在某个文件夹里搜索结果时，第一行“在此文件夹批量重命名…”打开窗口，列出该文件夹的直接子项。
- 关键词：输入 `br`、`pl`、`rename` 或“批量重命名”进入关键字模式。后面接着打名字，就是在 Win顺 的索引里按文件名搜索：第一行“批量重命名匹配的 N 项…”打开窗口改全部匹配，后面几行各打开一个文件；留空回车打开空窗口。
- 跨目录也能一起改：批内同名只按同一个目录比较，不同目录的同名文件互不冲突；窗口里每行带自己的目录，悬停可看完整路径。

## 权限

| 权限 | 用途 |
|---|---|
| `ui` | 插件自己的窗口：表格、规则栏、文本缓冲区 |
| `files:read` | 列出选中目录的子项、读文件名与时间/大小属性（改名前后核对文件身份），以及元数据模板用到的图片元数据（只对 jpg/jpeg/png/tif/tiff/heic/heif/avif，先按扩展名筛选） |
| `files:write` | 改名；删除经 Shell `IFileOperation` 移入回收站 |
| `winshun:search` | 关键词后打名字时，用 Win顺 的文件名索引搜索（`search_files`，只读名字与路径） |

不联网，不启动包外的程序，以普通权限运行。批量处理只改文件名，不改文件内容。

## Qt Widgets 窗口

窗口是 Qt Widgets：`ui/widgets_window.*` 是新的表格、规则栏和文本编辑器，`ui/qt_host.cpp` 只负责创建 `QApplication` 和窗口。核心、Presenter、Bridge 和执行器没变。构建仍用 `-DRENAME_BUILD_QT=ON`，但需要 Qt Widgets；`tools/pack.ps1` 会把 `Qt6Widgets.dll` 一起装进包（WinShun 的共享 Qt 契约目前还没有 Widgets）。实测、截图和未验证项见 [docs/validation/rename-widgets-20261115/README.md](docs/validation/rename-widgets-20261115/README.md)。

## Vim 模式

文本模式支持 Vim 按键，默认关闭。核心是独立的可移植库 [`winshun-vimcore`](https://github.com/ra9fael/winshun-vimcore)（纯 C++20，无 Qt、无 Win32），以 submodule 挂在 `vendor/vimcore`；Qt 适配层 `ui/vim_qt.*` 留在本仓库，选择记在 `<dataDir>/settings.json`。

支持 `h j k l w b e 0 ^ $ gg G f F t T %`、`i a I A o O s R`、`d c y` 配 motion（含 `f F t T` 与 `;`/`,` 重复，`d;`/`d,` 按上次查找）、文本对象与叠用（`dd cc yy D C S x X Y p P J r ~`）、`v`/`V`/Ctrl+Q 选区（含块选）与选区 `r` 一字符替换、计数前缀、`.` 重放、Ctrl+A/X 数字增减、Ctrl+D/U/F/B 翻页，以及 `:` 命令行（查找替换、`:s`、`:%s`、`:q`、`:q!`）。非插入模式下输入法不向缓冲区漏字。

按键边界、Esc 归属、撤销、输入法与测试见 [docs/VIM-MODE.md](docs/VIM-MODE.md)。

```bash
git clone --recurse-submodules <repo>      # 或者克隆后补一步：
git submodule update --init --recursive
```

没有 submodule 时 CMake 会直接报错并给出上面这行命令，而不是静默少掉 Vim 模式。

## 本地试用（不经宿主）

`rename-ui.exe` 是独立的窗口 wrapper，用来在没有 Win顺 的情况下验证界面与逻辑；它链接的正是插件窗口的同一份代码。

```
rename-ui.exe --dir <文件夹> [--recursive]      # 把文件夹的直接子项作为输入集
rename-ui.exe --files <路径> [<路径>...]        # 指定这几个文件
rename-ui.exe                                   # 空窗口
```

运行时需要 Qt：把 `C:\Program Files\WinShun` 加进 `PATH`，并设 `QT_PLUGIN_PATH`、`QT_QPA_PLATFORM_PLUGIN_PATH`、`QML_IMPORT_PATH` 指向同一目录（宿主 runner 就是这样做的）。

诊断钩子（仅用于验证）：`RENAME_GRAB_WINDOW=<png>` 渲染窗口后退出；`RENAME_GRAB_RULE=<查找>\n<替换>`、`RENAME_GRAB_MODE=text`、`RENAME_GRAB_APPLY=1` 分别预置规则、切到文本模式、执行后再截图。

## 现有基线

本提案以 `tmp/rename-comparison/day.rename/README.md`、`rename_core.py`、`plugin.json` 为事实来源：day.rename 已支持 Python 正则多规则、智能整理、日期/文件元数据、多种编号、手动多行编辑、可选 AI 命名、冲突不覆盖，以及两阶段改名和日志恢复；它不是"毫无功能"的空白基线。当前触发来自插件命令，不是 manifest 的 `files` 触发；明确不做递归；当前是 all replacements，没有 extension-only 模式。

## 两种输入方式

插件提供**双模式**，共用同一套收尾逻辑与同一个执行器，所以模式只影响你怎么输入，不影响结果正确性。

**控件模式**——用规则链和标志位描述"怎么改"。适合结构化重命名：照片编号、日期模板、批量前缀/后缀、扩展名统一、大小写变换。

**文本模式（oil 模式）**——把文件集摊成文本缓冲区直接编辑，借鉴 [oil.nvim](https://github.com/stevearc/oil.nvim)。每行一个条目、带不可编辑的 `/<id>` 锚点前缀；改完按 `Ctrl+S` 才落盘。适合零散修正、从表格或脚本粘贴结果、一次性肉眼核对。

### 借鉴 oil.nvim 的五条

1. **文本本身就是数据模型**——缓冲区既是输入也是预览，不用维护"表单 + 预览表"两套状态。
2. **`/<id>` 锚点是一切的前提**——纯文本里把 `a.txt` 改成 `b.txt`，和"删掉一行再加一行"在字节层面无法区分。锚点是唯一能表达"还是那个对象，只是换了名字"的手段。
3. **差异（diff）而非命令是中间表示**——解析只产 `new/delete/change`，diff→动作独立成层，确认/进度/撤销都能挂在 diff 上。
4. **错误结构化、逐行、不阻断**——行号+列号+消息，一行坏不影响别人，错误在缓冲区内高亮。
5. **落盘必须显式触发并确认**——编辑 ≠ 提交，前面还有确认弹窗和进度窗口。

## 借鉴 PowerRename 的五条

参考本地 PowerToys 检出（`PowerToys/src/modules/PowerRename/`，只读，不引入源码）：

1. **规则核心是纯函数**——`Replace(source, flags, counter)` 不碰磁盘，所以可以每键重跑。所有行为开关压进一个位标志。
2. **预览在独立线程、可取消**——"打字打到一半"永远安全。
3. **执行交给 Shell `IFileOperation`**——改名、撤销、提权都委托系统，不自己写恢复日志。
4. **深度倒序提交**——子项先改、父项后改、改完修正子树路径，父子目录同时改名不再需要整批拒绝。
5. **收尾三步（trim → 大小写变换 → NFC 等价判定）**——正确处理 NFD 文件名，也让"仅大小写改名"安全可行。

## 刻意不跟随的地方

- **不做冲突自动改名**。PowerRename 用 `FOF_RENAMEONCOLLISION` 把冲突改成 `name (1)`；我们冲突即拒绝，绝不覆盖。
- **不抄它的死代码**。`ItemNameAlreadyExists` 枚举值在 PowerRename 里从未被赋值，我们把目标已存在判定真正实现。
- **文本模式只做改名**。oil.nvim 支持新建/删除/移动/跨目录，我们收敛到改名（删除走回收站），使风险面与控件模式相同。
- **不实现树视图**。照抄 oil.nvim 自己的判断：树视图是另一套方法论。
- **不写自研恢复日志**。改名与撤销都交给 Shell，崩溃后不自动回滚，只保留只读批次清单做诊断。

## 能力范围

规则作用范围：名称、扩展名、完整名称；匹配首个或全部；计数模板和日期模板；元数据模板（EXIF/XMP，限 8 种扩展名）；大小写变换四档；预览匹配高亮、选择、过滤、排序；规则链和预设。

执行设计明确处理取消、交换名、父子目录组合、仅大小写变更、Unicode 与 NFD、Windows 保留名、执行期间外部修改及 SMB 行为。撤销走 Shell 撤销栈，撤销前重新核对文件身份，不覆盖后来创建的文件。AI 不在核心功能内。递归收集默认关闭，显式开启后仅收集选定根下文件、不自动改目录。文件仅最后后缀为 extension（`archive.tar.gz` 为 `gz`），`.gitignore` 无后缀；无后缀 extension 规则默认跳过并解释，目录 extension 不适用。尾部空白与点按收尾规则去除，但预览显式提示，不静默。

## 源码与构建

本版本源码是 tag `v0.4.5`（commit `046f77b`）。构建、测试并打包：

```powershell
git submodule update --init --recursive
cmake -S . -B build-qt -G Ninja -DCMAKE_BUILD_TYPE=Release -DRENAME_BUILD_QT=ON `
  -DCMAKE_PREFIX_PATH=D:\tools\Qt\6.12.0\msvc2022_64
cmake --build build-qt --parallel
ctest --test-dir build-qt --output-on-failure
pwsh -File tools/pack.ps1 -BuildDir build-qt -OutDir dist
```

`ctest` 是 7 个目标：`vim_qt`、`vim_window`、`rename_core`、`rename_e2e`、`core_purity`、`action_payload`、`manifest_menu_switch`；`vendor/vimcore` 自带的两套测试单独构建（`-DVIMCORE_BUILD_TESTS=ON`）。

发布由 `.github/workflows/release.yml` 完成：推一个 `v<版本>` tag，windows-2022 上装 Qt 6.12、构建、跑测试、用同一个 `pack.ps1` 打包，再把 `.wsplugin` 与它配对的 `.sha256` 附到 Release。`workflow_dispatch` 只验证、不发布。

## 已知问题

- 真实鼠标交互（右键菜单进入、表格勾选、目录栏、确认对话框）用宿主同环境的模拟进程与渲染图验证，没有逐项手点。
- 非插入模式的输入法隔离依赖平台层解除输入法绑定；中文输入法的真机组合行为未逐项实测。
- ex 命令行只到 `:`（查找/替换）、`:s`、`:%s`、`:q`、`:q!`；命名寄存器、宏、marks、`/`/`?` 搜索、多缓冲区不做。
- `Qt6Widgets.dll` 暂随包分发，共享 Qt 契约纳入 Widgets 后应移除这一项。

## 文档索引

- [设计与 11 节需求](plugin-design.md)：事实（含两份借鉴对象的逐条来源）、双模式架构、需求 ID、权限、失败矩阵、诚实笔记。
- [UI 规格](UI-SPEC.md)：两种模式各一张 ASCII 线框、布局、字段、快捷键、错误态、主题/缩放与无障碍。
- [测试计划](TEST-PLAN.md)：逐行 ID、FR、L0-L5、前置、步骤、预期、证据、未执行状态；含"与借鉴对象的行为差异"防错清单。
- [本地验证规范](TEST-SPEC.md)：装进宿主之前本地必须验证到什么程度（S1–S9），含目录输入集与单文件路径的断言。
- [Vim 模式](docs/VIM-MODE.md)：取舍、按键边界、输入法与测试。
- [发布记录 0.4.4](docs/validation/rename-release-20261117/README.md)：tag、CI 运行、产物校验与本机测试。
