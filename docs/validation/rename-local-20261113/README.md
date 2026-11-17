# 本地逻辑验证记录（2026-11-13）

规范：[TEST-SPEC.md](../../TEST-SPEC.md) · 本轮范围：S1–S10。

## 结论

| 层 | 用例 | 结果 |
|---|---|---|
| S1–S6 纯核心 + 视图模型 | `rename_core` 86 条 | 全绿（`unit-all.txt`） |
| S7 目录作为输入集 | 9 条断言 | 全绿（`s7-s8-directory.txt`） |
| S8 单文件最简路径 | 9 条断言 | 全绿（`s7-s8-directory.txt`） |
| S2 平台边界（真实改名） | 45 条 e2e 断言 | 全绿（`e2e-all.txt`） |
| S9 GUI 冒烟 | 6 条 | 全绿（`gui-s9-*.png`） |
| S10 宿主往返（行携带的 `data`） | 2 条路径 + 门禁 | 全绿（`host-sim-*.txt`） |

合计：`ctest` 4/4 通过，`rename_e2e` **45 passed / 0 failed**，`rename_tests` 86 条全过。

## S7：目录作为输入集（新能力）

`EnumerateRequest::listContents` 是本轮唯一新增的平台语义：根是文件夹时，它代表**它的直接子项**，而不是它自己。这就是"在此目录打开重命名界面"的含义。

实测（`s7-s8-directory.txt`）：

- 含 2 文件 + 1 子目录的文件夹 → 3 行；文件夹自身**不**出现；每一行 depth 都是 0
- 子目录的内容**不**列出（`deep.txt` 不在结果里），子目录本身作为一行且 `isFolder`
- 空文件夹 → 0 行、不报错
- `roots` 同时给文件夹和其中某文件 → 该文件只出现一次（按折叠路径去重）
- 不存在的根 → `blocked=true` 且带原因
- `listContents=false` → 文件夹本身仍是那一行（旧行为不变）
- 对目录内容批量改名 → 3 项真的改名；被改名的子目录里 `inner.txt` 位置不受影响（全部 depth 0，无父子重排）

## S8：单文件最简路径

- 一个文件 + `report→summary` → 一行 `Ready`，目标 `single_summary.txt`
- 查找词不存在 → `Unchanged`
- 目标已存在 → `Refused`，源文件不动，不产生 `name (1)`
- 干净改名 → 磁盘真变

## S9：GUI 冒烟（本地 wrapper，不经宿主）

`rename-ui.exe` 是本轮新增的**独立窗口 wrapper**（`ui/main-ui.cpp`）。它不是插件：`plugin.json` 只认一个 `runtime.program`，所以它不进包；它存在的意义是让窗口在这台机器上能被直接打开、驱动、截图，而不需要宿主、协议或 `.wsplugin`。它链接的是同一份 `rename_qml` + `rename_win`，因此走的正是插件窗口的代码路径。

用法：

```
rename-ui.exe --dir <folder> [--recursive]
rename-ui.exe --files <path> [<path>...] [--recursive]
rename-ui.exe                                  # 空窗口
```

证据：

| 文件 | 场景 | 结果 |
|---|---|---|
| `gui-s9-01-dir.png` | `--dir` 一个含 4 文件 + 1 子目录的文件夹 | 5 行，子目录内容未展开，底栏"5 项不变" |
| `gui-s9-02-rule.png` | 同上 + 规则 `IMG_ → photo_` | 4 行"待改"，`sub` 不变，底栏"4 项会改"，按钮"重命名 4 项"，**输入框回填 IMG_ / photo_** |
| `gui-s9-03-apply.png` | 同上 + 执行 | 磁盘 `draft_*.txt → final_*.txt`；表格与底栏都显示"已重命名 3 项"，不再自相矛盾 |
| `gui-s9-04-files.png` | `--files beta.txt gamma.txt` | 正好两行，顺序与参数一致，未混入 alpha |
| `gui-s9-05-text.png` | 文本模式 | 缓冲区 `/1 note_1.md` `/2 …` `/3 …`，行数与预览一致 |
| `gui-s9-06-textmode-dark.png` | 深色 fluent 调色板 | 换肤生效 |
| `gui-empty.png` | `rename-ui.exe` 无参数 | 空窗口，中心提示与底栏"还没有文件"一致 |

## 界面（按 PowerRename 布局重做）

演示稿：[rename-powertoy-layout.html](../../../../.zcode/v2/visualizations/059d20d2c141065e8cb6a82d2012d6c68ed981e09abe40e46af68091edfa0508/rename-powertoy-layout.html)

左栏一列规则（查找 / 三个开关 / 替换 / 应用于 / 文本格式 / 时间与元数据），右侧两列表格（原始 → 重命名后）。四个状态实测：

| 文件 | 状态 | 结果 |
|---|---|---|
| `layout-1-empty.png` | 刚打开，规则为空 | 4 行"不变"，主按钮禁用 |
| `layout-2-preview.png` | `IMG_ → photo_` | 3 行目标名加粗，主按钮"重命名 3 项" |
| `layout-3-conflict.png` | 目标已存在 | 该行红字说明，其余照常，状态栏警示 |
| `layout-4-done.png` | 执行完成 | 表格清空并报告结果，主按钮收起 |

## 依赖链与换名（本轮修复的真实缺陷）

窗口里抓到：`报告.pdf → 2026报告.pdf`，而 `2026报告.pdf` 就在同批里（自己会变成 `20262026报告.pdf`）。原实现把这判为"目标已存在"拒绝——但那是**顺序问题**，不是冲突。

修复分两层：

1. **计划层**（`core/plan.cpp`）：目标等于另一行**当前名字**时，若那一行也在改名，记 `waitsForId`（谁让出这个名字），不判冲突；只有"让出者不动"或"互相等待成环"才拒绝。
2. **执行层**（`win/execute.cpp`）：分批提交。每轮只提交当前目标确实空着的项，提交完再规划下一轮，这样"让出 → 占用"能自然落地。

证据：`s7-s8-directory.txt` 的 LS-S7-10（链式重命名 3 项全部成功）、LS-S7-11（换名成环，两项都拒绝、文件保持原样）。`rename_tests` 86 条含 `PlanWaitsForTheRowThatFreesItsTarget`。

## 插件测试流程（宿主 runner 环境）

`plugin-host-roundtrip.txt`：用 `WinShun/src/plugin/runner/Sandbox.cpp::withHostQt` 的同一组环境变量（`PATH` 前置宿主目录、`WS_QT_DIR`、`QT_PLUGIN_PATH`、`QT_QPA_PLATFORM_PLUGIN_PATH`、`QML_IMPORT_PATH`、`QT_QUICK_CONTROLS_STYLE=Basic`）启动插件，走完整 `initialize` → `query`（带 `filters.folder`）。

- initialize 44 ms 回应（清单 `timeoutMs` 1500）
- query 返回两行，第二行 `rename.here` 带 `openFolder`
- 全程无 stderr

`tools/host-sim.ps1` 现在能完整跑通两条路径，证据分别在 `host-sim-folder.txt` 与 `host-sim-one-file.txt`：

| 路径 | 断言 | 结果 |
|---|---|---|
| 文件夹行 | 回应 `hide`（真开窗），而不是"没有选中文件" | 通过 |
| 单文件行 | 回应"已重命名 1 项"，且磁盘上 `report.txt` 真的变成 `summary.txt` | 通过 |

## 行携带的 `data`（本轮修复的第二个真实缺陷）

宿主把 item **原样**回传（`WinShun/src/plugin/Items.cpp::itemToJson`），插件放进 `data` 的字段回来时是 `item.data.*`。而 `action()` 原来只读顶层 `a.item["batch"]`，于是对一行明明带着 batch 的结果回答 **"No files selected"**。

这个缺陷不会大声失败：它看起来像界面没反应，实际是读错了层级。纯核心测试也覆盖不到——只有协议往返能抓到。

修复：`action()` 先取 `payload = a.item["data"].isObject() ? a.item["data"] : a.item`，再读字段；保留顶层回退，让 once 插件那种形状仍然可用。并加了 `tools/check-action-payload.ps1` 作为 ctest 门禁（`action_payload`），负向验证过：把 `payload["batch"]` 改回 `a.item["batch"]` 立刻 FAIL。

四条新用例（LS-S10-01…04，`TEST-SPEC.md`）覆盖两种形状与宿主不改写行内容。

## 另一个脚本自身的缺陷

`tools/host-sim.ps1` 原来会**卡死**：PowerShell 变量名不区分大小写，参数 `[string]$Query` 和脚本里的 `$query` 是同一个变量，赋值 hashtable 时被强制转回字符串，发出去的是 `"System.Collections.Hashtable"`。插件读不懂，脚本就一直阻塞在 `ReadLine()`。

修复：参数改名 `$QueryText`，消息变量改名 `$queryMsg`；`ReadLine` 全部换成带超时的 `Read-Line`（`ReadLineAsync().Wait()`），超时即 kill 并记 FAIL，不再无声挂住。

包：`dist/ra9fael.rename-0.1.0.wsplugin`，sha256 `911fbdc9…e9f00`，214,707 字节；包内 exe 经 dumpbin 确认导入 Qt6Core（共享 Qt 契约）。

## 本轮修掉的问题

1. **`listContents` 的路径拼接**：`FindFirstFileW` 的模式漏了分隔符（`C:\dir*` 而非 `C:\dir\*`），导致目录枚举全空 —— 4 条断言失败后定位并修正；盘符根 `C:\` 也一并处理。
2. **不存在的根不报错**：原来静默返回空集，现在 `blocked` 并给出"找不到：…"。
3. **执行后底栏自相矛盾**：表格说"已重命名 3 项"，底栏同时说"还没有文件"。现在 `doneLine` 非空时底栏不再报阻断原因，且"重命名 N 项"按钮隐藏（没有可执行的剩余项，禁用按钮会被读成失败）。
4. **规则输入框不回填**：规则从外部设置（wrapper 命令行、将来的预设）时，四个输入框是空的，而预览已经用了规则——用户看到的输入与结果对不上。现在 `ruleFind/ruleReplace/ruleScope/ruleMatch/ruleEnabled` 是 Bridge 的属性，QML 双向绑定。

## 搜索栏收窄（按需求）

搜索栏现在只保留两件事，批量与 oil 全部进窗口：

| 输入 | 结果 |
|---|---|
| `rename` | 两行：`改名：rename 原名 -> 新名`、`在文件夹里重命名…` |
| `rename report -> 报告` | **单行**预览（匹配到唯一文件时）；多个匹配则明确说"匹配到 N 个文件，搜索栏只改一个文件" |
| `rename report` | `还需要新名字`，提示写成 `rename report -> 新名字` |

协议实测见 `keyword-modes.txt`。

## 未验证（本轮不做，写下来避免误读）

- **在真实 WinShun 窗口里点过**：`files` 触发、`appearance` 真实换肤、提权、Shell 撤销栈、SMB、DPI、读屏都没有在宿主 GUI 里手动走过。宿主 runner 的**环境变量与协议往返**已验证（`host-sim-*.txt`），但那不等于在宿主窗口里点一遍。
- **S9 只覆盖了 wrapper 路径**：窗口在宿主里由同一个 `Main.qml` + `Bridge` 渲染，但宿主环境的进程边界、线程模型与权限没有跑过。
- **`--recursive` 只做了代码审查**，没有 e2e 断言。
- **UI 交互细节**（用鼠标点复选框、拖选、滚动）没有自动化；S9 只验证了数据流与渲染。
- **light / brand 主题**：按用户要求只验了 dark fluent 一种。
- **`.wsplugin` 的安装流程**：包已生成并校验内容，`未在宿主安装器里走一遍`（本轮是把 exe 直接复制进插件目录）。

## 证据文件

- `ctest-all.txt` — `ctest --output-on-failure` 原始输出（4 个测试）
- `unit-all.txt` — `rename_tests` 86 条
- `e2e-all.txt` — `rename_e2e` 45 条断言（含 S7/S8/S10）
- `s7-s8-directory.txt` — S7/S8 详细断言原始输出
- `keyword-modes.txt` — 搜索栏三条路径的协议往返
- `host-sim-folder.txt` — 文件夹行：`hide` + "没有选中文件"负向断言
- `host-sim-one-file.txt` — 单文件行：真改名成 `summary.txt`
- `plugin-host-roundtrip.txt` — 宿主环境 initialize/query 往返
- `layout-1-empty.png` … `layout-4-done.png` — PowerRename 布局四态
- `gui-s9-01-dir.png` … `gui-s9-06-textmode-dark.png` — 窗口渲染
