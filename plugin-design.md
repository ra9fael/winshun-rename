# ra9fael.rename 插件设计提案

**状态：需求文档；核心逻辑已实现并测过，宿主集成未验证。** 日期：2026-11-10。遵循工作台 `DEVELOPMENT.md` 的 11 节设计骨架。英文：[plugin-design.en.md](plugin-design.en.md) · [README](README.md) · [UI 规格](UI-SPEC.md) · [测试计划](TEST-PLAN.md) · [实施验证](docs/validation/rename-m0-20261110/IMPLEMENTATION.md)。

## 1. 关键裁决（Verdict）

以 native C++/Qt 共享 runtime 提供一个**双模式**批量改名插件：**控件模式**用规则链与标志位描述"怎么改"，**oil 模式**把文件集当文本缓冲区直接编辑（借鉴 oil.nvim 的 `/<id>` 锚点法）。两种模式产出同一种不可变预览计划，交给同一个 Shell 执行器（`IFileOperation`，深度倒序提交），因此**模式选择不影响正确性，只影响输入方式**。

入口只用宿主正式支持的 `files` 触发，不伪称已有 Explorer 右键集成。所有目标必须在任何磁盘变更前完成规划、冲突分析和用户确认。首版 oil 模式**只允许改名**，拒绝新建/删除/移动/链接改动——这把两种模式的风险面压到相同。首版 AI 不核心化；Explorer 集成只列探索阶段。

## 2. 依据的实测事实（Grounded facts）

### 2.1 现有基线（day.rename）

| ID | 事实 | 来源与证据级别 |
|---|---|---|
| RNF-01 | day.rename README 描述其已有本地查找替换、智能整理、插入、自动编号、手动编辑、可选 AI，并先预览再执行、支持撤销。 | `tmp/rename-comparison/day.rename/README.md` §1-§5；实测读取 |
| RNF-02 | `rename_core.py` 使用 Python `re`；替换规则按顺序逐条执行，正则调用 `re.sub`，因而当前规则会匹配全部 occurrence。 | `rename_core.py` L116-L126；源码实测读取 |
| RNF-03 | day.rename 支持 NFKC、空白/分隔符整理、开头编号清理、大小写变换、文件创建/修改日期等元数据、阿拉伯/中英数字/字母编号和模板，以及完整名称手动输入。 | `README.md` §4-§5、`rename_core.py` L57-L113、L127-L162；实测读取 |
| RNF-04 | day.rename 以随机临时名称两阶段执行、拒绝覆盖目标、记录文件身份与路径日志、失败尝试恢复；恢复前验证原名可用。它明确不是文件系统整体原子事务。 | `README.md` §执行与恢复；`rename_core.py` L184-L213、L216-L321；实测读取 |
| RNF-05 | day.rename manifest 目前是 resident Python 插件，命令 keyword 为 `pl`/`cmm`，无 `files` trigger 声明。 | `plugin.json` L1-L42；实测读取 |
| RNF-06 | day.rename 说明添加文件夹不递归改动内部内容；当前无 extension-only scope。README 表示默认保留文件最后扩展名，可选"同时修改扩展名"；文件夹按完整名称处理。 | `README.md` §文件选择和扩展名；实测读取 |
| RNF-07 | 宿主 `files` 触发只有路径数组：`trigger` 与 `action` 都是 `files`，`WS_FILES` 是路径 JSON 数组，整个数组约 32000 字符上限，超出时宿主提示少选一些；**没有 item 概念**，所以"选中项"必须由插件自己维护。 | `WinShun/docs/plugins.md`「触发」「协议·一次性」；实测读取 |
| RNF-08 | 宿主为有自己窗口的常驻插件提供 `ui` 权限、`appearance` 消息、共享 Qt 契约（`runtime.qt: "host"`，保 Qt Core/Gui/Qml/Quick 与 `QtQuick.Controls.Basic`）。 | `WinShun/docs/plugins.md`「有自己窗口的常驻插件」「原生插件的共享 Qt 契约」；实测读取 |

### 2.2 借鉴对象之一：PowerToys PowerRename

本地检出：`PowerToys/src/modules/PowerRename/`（只读参考，不修改、不引入其源码）。

| ID | 事实 | 来源 |
|---|---|---|
| RNF-09 | 规则核心是纯替换函数 `Replace(source, &result, enumIndex)`：输入字符串与位标志，输出新名字与递增计数索引，不碰磁盘。 | `lib/PowerRenameInterfaces.h` L9-30、`lib/PowerRenameRegEx.cpp` L435-634 |
| RNF-10 | 所有行为开关压进一个 `DWORD`：大小写敏感、全部匹配、正则、枚举、排除文件/文件夹/子文件夹、只改名/只改扩展名、四种大小写变换、随机、三种时间戳、两种元数据源；整体存进设置。 | `PowerRenameInterfaces.h` L9-30、`lib/Settings.h` |
| RNF-11 | 项状态是 5 值枚举 `Init / ShouldRename / ItemNameTooLong / ItemNameInvalidChar / ItemNameAlreadyExists`；`ShouldRenameItem` 是纯判定（已选 且 可改 且 真变了 且 未排除 且 状态允许）。**注意 `ItemNameAlreadyExists` 全代码从未被赋值，是死代码。** | `PowerRenameInterfaces.h` L41-47、`lib/PowerRenameItem.cpp`；`Renaming.cpp` 全文检索无赋值 |
| RNF-12 | 可见性独立于状态：`IsItemVisible(filter, flags)`，过滤器 `None / Selected / FlagsApplicable / ShouldRename`，表头点击切换；过滤只改可见行，不改集合。 | 同上、`lib/PowerRenameManager.cpp` `SwitchFilter` |
| RNF-13 | 预览在独立工作线程：任一输入变化（搜索词、替换词、标志、时间、元数据）触发 `_PerformRegExRename()`，取消旧线程起新线程，用 `PostMessage` 回状态，主线程泵消息。 | `PowerRenameManager.cpp` L439-467、L930-990 |
| RNF-14 | 执行不自己写改名循环，整批交给 Shell `IFileOperation`，标志 `FOF_ALLOWUNDO \| FOFX_ADDUNDORECORD \| FOFX_SHOWELEVATIONPROMPT \| FOF_RENAMEONCOLLISION`；撤销由 Shell 撤销栈提供。 | `PowerRenameManager.cpp` L16、L848-860 |
| RNF-15 | 按深度倒序提交：先算最大深度做深度矩阵，从最深一层往第 0 层加项，子项先于父项改名；父目录改完再 `UpdateChildrenPath` 修正子树路径。 | `PowerRenameManager.cpp` L757-800、`UpdateChildrenPath` |
| RNF-16 | 计数索引跨整批共享，且只在真的发生匹配时自增（`shouldIncrementCounter`）；没匹配的项不占号。 | `PowerRenameRegEx.cpp` L583-605 |
| RNF-17 | 名字拆分/回拼是显式两段：`NameOnly` 取 stem 送替换、回来拼回扩展名；`ExtensionOnly` 取扩展名（去点）送替换、回来拼回 stem；无扩展名 + `ExtensionOnly` 直接返回原名。 | `lib/Renaming.cpp` L107-115、L219-235 |
| RNF-18 | 结果必经三步收尾：`GetTrimmedFileName` 去首部空白、去尾部空白与点 → `GetTransformedFileName` 做大小写变换 → `IsSameName` 判定等价。 | `Renaming.cpp` L245-266 |
| RNF-19 | 等价判定用 Unicode NFC 归一化后逐字符比较，刻意不用 `lstrcmp`（`ß` 与 `ss` 判为不同）；输入统一走 `SanitizeAndNormalize`（NBSP→空格、NFC）。 | `Renaming.cpp` L12-33、`PowerRenameRegEx.cpp` L23-55 |
| RNF-20 | 非法字符是硬编码表 `< > : " \ / \| ? *`；长度按 260（文件）/247（目录）算增量：`原路径长度 + (新名长度 - 原名长度)`。 | `Renaming.cpp` L286-299 |
| RNF-21 | 日期模板：`$YYYY $YY $Y $MMMM $MMM $MM $M $DDDD $DDD $DD $D $HH $H $hh $h $mm $m $ss $s $fff $ff $f $TT $tt`；月份/星期名走 `GetDateFormatEx` 取用户区域语言；`$$` 转义；长模式先于短模式，并用负向前瞻避免 `$D` 撞 `$DATE_TAKEN_*`、`$H` 撞 `$HEIGHT`。 | `lib/Helpers.cpp` L393-505 |
| RNF-22 | 枚举模板 `${start=N,increment=N,padding=N}` 用正则抓取并记录在替换串里的字节偏移，替换时按偏移插入；`padding` 上限取模 `MAX_PATH`，缓冲不够回退 0 填充。 | `lib/Enumerating.h`、`lib/Enumerating.cpp` |
| RNF-23 | 元数据（EXIF/XMP）只对 `jpg/jpeg/png/tif/tiff/heic/heif/avif` 生效，先按扩展名快筛再查模板，走 WIC 提取并带缓存；取不到值时把 `$PATTERN` 原样留在名字里，不静默替换成空。 | `Helpers.cpp` L322-390、L513-592 |
| RNF-24 | 正则支持 `std::wregex` 与 `boost::wregex`，均以 ECMAScript 模式编译，靠设置切换；"只匹配首个"用 `format_first_only`。 | `PowerRenameRegEx.cpp` L421-434 |
| RNF-25 | 正则核心有 fuzz 入口（三段切分输入/查找/替换）与 22 个 manager 用例、40 个 helper 用例。 | `PowerRename.FuzzingTest/`、`unittests/` |

### 2.3 借鉴对象之二：oil.nvim

在线读取 `github.com/stevearc/oil.nvim`（只读参考，不引入其源码）。

| ID | 事实 | 来源 |
|---|---|---|
| RNF-26 | 核心主张：把目录当普通缓冲区编辑——打开目录后每行一个条目，随意改文本，`:w` 才真正落盘。 | README「Quick start」 |
| RNF-27 | **每行以 `/<id> ` 锚点前缀开头**（如 `/123 filename.txt`）。解析时据 id 认出"这是同一批条目里的哪一个"；同 id 换名 = 改名，id 消失 = 删除，**无 id 的行一律视为新建**。这是纯文本编辑能区分"改名"与"删了再建"的唯一依据。 | `lua/oil/mutator/parser.lua` `parse_line`、`M.parse` |
| RNF-28 | 目录行以 `/` 或 `\` 结尾；符号链接写成 `name -> target`。 | `parser.lua` `parsedir`、`parse_line` |
| RNF-29 | 解析只产三类差异：`new`（带 id = 改名，不带 id = 新建）、`delete`（原行消失）、`change`（列值变化）；改名被拆成"同 id 的 new"。 | `parser.lua` `M.parse`、`mutator/init.lua` |
| RNF-30 | 差异→动作映射独立成层：`new` 带 id → `move`（src→dest）；`new` 无 id → `create`；`delete` 挂同 id diff 成 `has_delete` → `delete`；`change` → `change`。 | `mutator/init.lua` `create_actions_from_diffs` |
| RNF-31 | 重名检查按文件系统语义归一：Windows/macOS 下先 `lower()` 再比，重复即报 `Duplicate filename`。 | `parser.lua` `check_dupe` |
| RNF-32 | 解析错误是行号+列号+消息的结构化表，不抛异常；一行坏不影响其他行。错误类型含：ID 格式坏、找不到对应条目（id 被改过）、名字含路径分隔符、路径以 `/` 开头、重复名。 | `parser.lua` `M.parse` 的 `errors` |
| RNF-33 | 写入前有确认弹窗；`skip_confirm_for_simple_edits` 可跳过简单操作的确认，`prompt_save_on_select_new_entry` 在选中条目时提示先保存。 | README 选项表 |
| RNF-34 | 执行时有独立进度窗口，逐条推进（`progress:set_action(action, idx, #actions)`），可中途取消。 | `mutator/init.lua` L386-490 |
| RNF-35 | 刻意不做树视图。README 明确：树视图需要完全不同的一套方法，等于重写。 | README「FAQ」 |
| RNF-36 | 解析时缩进被完全丢弃（`vim.trim`），仅作视觉层次；跨目录编辑靠新行里的嵌套路径与花括号交替。 | `parser.lua`、`mutator/init.lua` |

## 3. 架构

### 3.1 目标形态（待批准）

native C++/Qt 共享 runtime（`runtime.qt: "host"`），接入 Win顺真实支持的插件模板。文件集由宿主 `files` 触发提供（RNF-07）：只有路径数组，没有 item，所以选中状态与 oil 模式的 id 表都由插件自己维护。宿主契约、`files` schema、权限与最低版本仍需实施前从真实宿主核实；若宿主不具备该入口，不可退而声称已有 Explorer 集成。

纯规则核心不依赖 Qt 或 OS 头文件，输入文件名/元数据/规则快照（或 oil 缓冲区 + id 表），输出变更计划、匹配范围、警告和冲突；C++/Qt UI 只编辑规则或文本、呈现预览、收集确认。Windows 执行适配层负责身份检查与提交；**不写自己的两阶段改名**，改名与撤销都交给 Shell `IFileOperation`（RNF-14）。UI 不直接修改文件。

### 3.2 双模式结构

两种模式共享同一个规则核心与同一个执行器，只有"改名来源"不同。

```text
                     +----------------------------------+
                     |  rename_core  (纯 C++, 无 OS)      |
                     |  RuleEngine / TemplateExpander    |
                     |  NameSplitter / NfcCompare        |
                     |  OilParser / OilDiffer            |
                     |  StatusEvaluator / PlanBuilder    |
                     +----------------------------------+
                                  ^
                                  | 产出 PreviewPlan (不可变)
                     +------------+-------------+
                     |                          |
        [控件模式 ControlMode]        [oil 模式 OilMode]
         规则链 + 标志位               文本缓冲区解析
         → Evaluate()                 → Diff → Plan
                     |                          |
                     +------------+-------------+
                                  |
                                  v
                     +----------------------------------+
                     |  rename_win  ShellExecutor        |
                     |  IFileOperation, 深度倒序提交       |
                     +----------------------------------+
```

关键点：**oil 模式不走规则引擎**。它的"旧名→新名"由用户直接写出，不需要 `Replace()`。但它**复用同一套收尾逻辑**（trim / 大小写变换 / NFC 等价 / 非法字符 / 长度 / 状态枚举）与**同一个执行器**。这让两种模式的正确性论证只需做一次。

模式切换在同一个窗口内进行，切换时保留已选文件集；切换会丢弃另一模式未提交的编辑，必须先确认。

### 3.3 稳定需求 ID 与优先级

| ID | 优先级 | 需求 | 验收摘要 |
|---|---|---|---|
| RN-FR-01 | P0 | 仅由宿主 `files` 触发启动；清晰显示来源集合 | 入口不虚构 Explorer 支持；选择性由插件自维护且与宿主数组一致；超出约 32000 字符上限时提示少选 |
| RN-FR-02 | P0 | 规则 scope: name/extension/full；每步重新拆分 | 每种 scope 的输入范围及目录策略有明确预览与测试；尾随空白/点按收尾规则处理并显式提示 |
| RN-FR-03 | P0 | 单规则首个/全部匹配，规则链按序执行 | 首个/全部结果差异固定；每条链以明确前一输出为输入；计数索引作用域明确 |
| RN-FR-04 | P0 | 预览：去抖 + 后台线程 + `planId` 失效 | 预览与核心输出一致；旧计划立即失效；仅选中且有效项目可执行 |
| RN-FR-05 | P0 | 冲突/非法名称/路径/保留名检查 | 显示稳定错误；不覆盖任何已有项目；**真正实现目标已存在判定** |
| RN-FR-06 | P0 | 执行交 `IFileOperation`；撤销走 Shell 撤销栈 | 不加自动改名冲突标志；取消/失败不覆盖后来文件；批次清单可审计 |
| RN-FR-07 | P0 | 交换名、仅大小写变化、Unicode、父子选择与 SMB 边界 | 支持范围经 Windows/SMB 实测；不安全场景明确拒绝 |
| RN-FR-08 | P1 | 日期与计数模板 | 模板解析纯逻辑确定；模式清单含负向前瞻；无效字段预览为错误，不执行 |
| RN-FR-09 | P1 | 规则链预设 | 预设 = 规则链 + 标志位；不含文件路径/隐私数据；格式版本化 |
| RN-FR-10 | P1 | 状态 / 可见性 / 选中三正交的过滤与排序 | 过滤不改变输入集合；表头点击切过滤器 |
| RN-FR-11 | P2 | 两种正则引擎（std / boost，均 ECMAScript） | 引擎可配置；明确声明子集逐例兼容；真实核心测试通过后才标记 |
| RN-FR-12 | P2 | AI 命名候选 | 首版非核心；未获批不得加入联网或数据外发权限 |
| RN-FR-13 | P3 | Explorer 右键集成探索 | 另立宿主/Windows 方案与验证；探索未完成前不宣传 |
| RN-FR-14 | P1 | 显式开启递归收集文件，默认关闭 | 关闭时零目录遍历；开启后显示根、数量与来源，不自动改目录；开关变化使预览失效 |
| RN-FR-15 | P0 | 父目录与后代同时改名：深度倒序提交 | 子项先改、父项后改、改完修正子树路径；不整批拒绝，但要提示存在父子关系 |
| RN-FR-16 | P1 | 元数据模板（EXIF/XMP） | 限 8 种扩展名；先按扩展名快筛；取不到值保留 `$PATTERN` 字面量 |
| RN-FR-17 | P1 | 大小写变换四档 | Uppercase / Lowercase / Title Case（带例外词表）/ Capitalize；目录同样适用 |
| RN-FR-18 | P0 | **oil 模式**：文本缓冲区编辑 + 锚点解析 + diff→改名 | 缓冲区可编辑、可粘贴；解析为结构化逐行错误；产出与控件模式同构的预览计划 |
| RN-FR-19 | P0 | **oil 模式安全边界**：首版仅允许改名 | 新建/删除/移动/链接改动一律判错误行并拒绝落盘；风险面与控件模式相同 |
| RN-FR-20 | P0 | **双模式共用执行器与收尾逻辑** | 两模式对同一最终名字产出同一状态；模式切换保留选择、丢弃未提交编辑需确认 |

目录可作为宿主显式选中的单个改名项目；仅 RN-FR-14 开启时遍历用户选定根目录收集文件。默认不跟随 symlink/junction，不越出根目录；重叠根按文件身份去重，读取失败标错而非静默执行不完整集合。递归不会自动勾选目录。

### 3.4 标志位布局

照抄 RNF-10 的布局，按首版裁剪：

```text
0x0001 大小写敏感        0x0002 匹配全部        0x0004 正则
0x0008 启用计数模板      0x0010 排除文件        0x0020 排除目录
0x0040 排除子目录内容    0x0080 只改主名        0x0100 只改扩展名
0x0200 转大写            0x0400 转小写          0x0800 Title Case
0x1000 Capitalize        0x2000 随机（首版裁掉）
0x4000 创建时间 / 0x8000 修改时间 / 0x10000 访问时间
0x20000 EXIF / 0x40000 XMP
```

裁掉 `0x2000` 随机的理由：不可复现，与"预览即最终结果"直接冲突。

### 3.5 规则与兼容模型（控件模式）

RN-FR-02 的固定语义：每步按上一规则输出重新拆分文件名。`extension` 仅最后一个点后的非空后缀（不含分隔点），且点前必须有名称；`archive.tar.gz` 为 name=`archive.tar`、extension=`gz`，`.gitignore` 与 `README` 无后缀。`name` 仅改主名并保留最后后缀及分隔点；`full` 改完整叶名称（不含路径）。无后缀文件的 `name` 等于 `full`；`extension` 默认跳过、不创建后缀，即使正则匹配空串也不应用，并显示"无后缀，扩展名规则未应用"。目录的 `name`/`full` 均是完整目录叶名称，`extension` 不适用、保持不变并说明。

**尾随空白与点（RNF-18 的裁决）**：跟随 PowerRename 的 trim——去首部空白、去尾部空白与点——但**预览必须显式提示"已去除尾部空格/点"**，不静默。这与"尾随点是非法名"并不矛盾：非法的是**结果**，trim 是**产生合法结果的手段**。

规则节点含稳定 `ruleId`、启用位、scope、匹配类型（literal/regex）、匹配文本、替换模板、case policy、occurrence policy。

### 3.6 收尾三步（两种模式共用）

顺序固定，不可调换：

```text
1. Trim          去首部空白；去尾部空白与点
2. Transform     按标志位做大小写变换（Uppercase / Lowercase / Title Case / Capitalize）
3. Compare       与原名做 NFC 归一化逐字符比较，等价则判为"无变化"
```

`Compare` 用 NFC 而非 `lstrcmp`（RNF-19）。这让 NFD 文件名被正确处理，也让"仅大小写改名"安全可行。`Transform` 只在结果仍为空或非法时才报错。

### 3.7 模板与计数

日期模式清单照抄 RNF-21：`$YYYY $YY $Y`、`$MMMM $MMM $MM $M`、`$DDDD $DDD $DD $D`、`$HH $H`（12 小时制）、`$hh $h`（24 小时制）、`$TT $tt`、`$mm $m`、`$ss $s`、`$fff $ff $f`。冲突规避照抄：长模式优先；`$D` 用负向前瞻排除 `DATE_TAKEN_`/`ESCRIPTION`/`OCUMENT_ID`；`$H` 排除 `EIGHT`。`$$` 转义。

计数模板 `${start=N,increment=N,padding=N}`（RNF-22）。**计数索引作用域——我们与 PowerRename 的唯一实质差异**：

- 索引按整批共享（跨项连续），不按规则重置；
- 一条链里出现多个计数模板时共享同一索引（同一项里两个 `${}` 印同一个号）；
- 只有"该链在该项上至少匹配一次"才自增（照抄 RNF-16 的直觉）。

### 3.8 oil 模式规约（本质 RNF-26…RNF-36，收敛为改名专用）

#### 缓冲区格式

```text
/1042 report-draft.txt
/1043 report-final.txt
/1044 photos/
      /1045 IMG_0001.jpg
/1046 notes.txt
```

- 每行 `/<id><空格><文本>`；`id` 由插件分配，进缓冲区时自动加，**用户不应改动**。
- 行尾 `/` 或 `\` = 目录（RNF-28）。
- 缩进在解析时全部丢弃，只为视觉层次（RNF-36）；**不实现树视图**（RNF-35）。
- 首版不支持 ` -> ` 链接语法（RNF-28 的可选项），出现即判错误行。

#### 锚点语义（RNF-27，oil 模式的命门）

| 缓冲区变化 | 判定 | 首版动作 |
|---|---|---|
| 同 id 换了叶子名 | 改名 | ✅ `RenameItem` |
| 某 id 的行消失 | 删除 | ❌ 拒绝：`首版不支持删除` |
| 出现无 `/<id>` 的新行 | 新建 | ❌ 拒绝：`首版不支持新建` |
| 行顺序变化 | 无差异 | ✅ 允许（顺序不代表重排） |
| `/<id>` 本身被改 | 找不到对应条目 | ❌ 拒绝：`该条目已不存在（id 被改过？）` |
| 叶子名含 `/` 或 `\` | 会移动位置 | ❌ 拒绝：`改名不能移动位置` |

**为什么会需要锚点**：纯文本里把 `report-draft.txt` 改成 `report-final.txt`，与"删掉一行、再加一行"在字节层面无法区分。锚点 `/<id>` 是唯一能表达"还是那个对象，只是换了名字"的手段。因此 id 表是唯一真相，缓冲区只是它的可编辑投影。

#### 解析与差异（RNF-29 / RNF-30）

```text
缓冲区文本 + id 表
      ↓ OilParser（逐行，出错不中断）
逐行结构 { id, leaf, isDir, rawLine, error? }
      ↓ OilDiffer
OilDiff[]: Rename{id, newLeaf} | Rejected{line, reason}
      ↓ 复用收尾三步 + 状态评估
PreviewPlan（与控件模式同构）
```

差异→动作层照抄 RNF-30 的分层：首版只保留 `Rename` 一种动作，`Create`/`Delete`/`Move` 全部在差异层就转成 `Rejected`，不进动作层。这样动作层只有一条路径，测试面大幅缩小。

#### 错误模型（RNF-32）

结构化、逐行、不阻断：

| 错误 | 行号 | 处理 |
|---|---|---|
| `/<id> ` 前缀格式坏 | ✓ | 该行标红，其余行照常解析 |
| 找不到对应 id | ✓ | `该条目已不存在（id 被改过？）` |
| 叶子名为空 | ✓ | 拒绝 |
| 名字含路径分隔符 | ✓ | 拒绝（改名不能移动） |
| 同批重复（按 FS 语义归一） | ✓ | 拒绝，同时标出两行 |
| 非法字符 `< > : " \ / \| ? *` | ✓ | 拒绝 |
| 长度超限（260/247 增量） | ✓ | 拒绝 |
| 目标已存在 | ✓ | 拒绝（对应 `ItemNameAlreadyExists`） |
| 无 id 的新行 | ✓ | `首版不支持新建` |
| 行消失 | ✓ | `首版不支持删除` |
| ` -> ` 链接语法 | ✓ | `首版不支持链接改动` |

错误全部在缓冲区内高亮，底部显示"N 处错误，无法保存"。任一错误存在即禁止提交。

#### 提交流水线

```text
用户编辑缓冲区
   ↓ (输入停止 300ms，仅"预检"开启时)
后台预检：parse → diff → 收尾三步 → 状态评估 → 行内标红 / 底部计数
   ↓
用户按 Ctrl+S / 点"应用"
   ↓
阻断检查（任一存在则拒绝，不落盘）：错误行 / 目标已存在 / 缓冲区同时出现改名与删除
   ↓
确认弹窗：N 改名 · 0 新建 · 0 删除 · M 错误
   ↓
执行器：IFileOperation，深度倒序，进度窗口可取消（RNF-34）
   ↓
完成后：缓冲区重置为新的 /<id> 行，id 重新分配，id 表同步更新
```

#### 与控件模式的差异

| 维度 | 控件模式 | oil 模式 |
|---|---|---|
| 输入 | 规则链 + 标志位 | 直接编辑目标名文本 |
| 走 `Replace()` | 是 | 否 |
| 批量能力 | 强（模板、计数、元数据、大小写变换） | 弱（靠用户自己写或粘贴） |
| 与外部编辑器协作 | 无 | 可整块复制粘贴 |
| 适用 | 结构化重命名（照片编号、日期、批量前缀） | 零散修正、从表格/脚本粘贴结果 |
| 预览形态 | 独立表格 | 缓冲区本身就是预览 |
| 共用 | 收尾三步、状态枚举、冲突检查、执行器、进度窗口 | 同左 |
| 首版动作面 | 改名 | 仅改名 |

## 4. 核心逻辑

### 4.1 纯核心 API

```cpp
// 控件模式：一次替换。纯函数，不碰磁盘，错误进 out 参。
struct ReplaceResult {
    std::wstring text;
    std::vector<MatchSpan> hits;   // 命中区间，给高亮用
    bool matched = false;          // 决定计数是否自增
    RuleError error = RuleError::None;  // BadRegex / BadTemplate / ...
};
ReplaceResult Replace(std::wstring_view source,
                      const Rule& rule,
                      const TemplateContext& ctx,
                      int* counter);

// 控件模式：规则链
ItemOutcome Evaluate(const ItemSnapshot& item,
                     const RuleChain& chain,
                     Flags flags,
                     int* counter);

// oil 模式：解析缓冲区 → 差异（逐行错误，不抛异常）
struct OilParseResult {
    std::vector<OilDiff> diffs;    // Rename{id,newLeaf} | Rejected{line,reason}
    std::vector<OilError> errors;  // line, col, message
};
OilParseResult ParseOilBuffer(std::wstring_view buffer,
                              const std::vector<OilEntry>& idTable);

// 两种模式共用：收尾三步，顺序固定
std::wstring Finalize(const std::wstring& candidate,
                      std::wstring_view originalLeaf,
                      Flags flags,
                      bool isFolder,
                      Status* status);
```

### 4.2 预览流水线（RNF-13）

```text
任一输入变化
   ↓ (50ms 去抖)
取消旧预览线程（置 cancel，等它退出）
   ↓
新线程：对每项 Evaluate() 或 应用 Diff → PreviewPlan
   ↓ 逐项通知 ITEM_READY(idx)（增量刷新表格）
   ↓ 全部完成通知 PLAN_COMPLETE
主线程：完成的计划替换旧计划，旧 planId 立即失效
```

执行只接受当前 `planId`。任何规则、缓冲区或输入变化都使旧 `planId` 作废。

### 4.3 执行流水线（RNF-14 + RNF-15）

```text
1. 冻结 PreviewPlan（planId 校验）
2. 逐项复核文件身份（大小/时间/卷序列），不符则该项失败
3. 按深度分组（depth = 相对选中根的层级）
4. 从最深一层向第 0 层，对 ShouldRename 的项调 IFileOperation::RenameItem
5. SetOperationFlags(ALLOWUNDO | ADDUNDORECORD | SHOWELEVATIONPROMPT)
   注意：不加 RENAMEONCOLLISION——冲突项在步骤 2 就已拒绝
6. PerformOperations()
7. 父目录改名成功后修正子树路径（UpdateChildrenPath 语义）
8. 成功后提供"撤销本批次" → Shell 撤销栈；失败则逐项报告当前位置
```

`JournalLite` 只写"批次 id、各项原路径/新路径/结果"，供诊断与报告；**不承担自动恢复**，崩溃后不尝试回滚，只提示用户用 Shell 撤销。这与 RNF-04 描述的 day.rename 自研恢复日志形成明确分界。

取消在安全边界生效：报告已完成、进行中、未开始；不承诺瞬时终止或整批原子回滚。

## 5. 交互

稳定命令/控件 ID、ASCII 线框、键盘行为见 [UI-SPEC.md](UI-SPEC.md)。入口默认只展示宿主传入的选中集合；不自动扫描目录。仅用户显式开启 RN-FR-14 后扩展为所选根目录下的文件，并展示来源与数量。

**控件模式**：规则编辑器显式选择 scope、first/all、regex/literal 和规则链顺序；标志位面板提供大小写变换四档、只改主名/扩展名、排除文件/目录/子目录；预览表显示原名、命中片段、目标名、状态，支持选择、过滤、排序；表头点击切换过滤器。

**oil 模式**：等宽文本编辑器，左侧 id 不可编辑，错误行标红并带波浪线，底部实时计数；`Ctrl+S` 提交并弹确认；执行时显示进度窗口可取消。

执行确认展示将改名数量、未匹配项、冲突项及目标摘要。完成后提供本批次撤销，撤销前重新检查状态。AI 只作为未来候选，首版默认无 AI 网络动作。

## 6. 权限、数据、隐私

权限文字草案（提案，非已生成 manifest）：`runtime.mode: resident`、`runtime.sandbox: normal`、`runtime.qt: "host"`；权限 `ui`（独立窗口）、`files:read`（宿主选集、规则元数据及显式递归根）、`files:write`（改名与批次清单）。依据 [宿主权限节](../WinShun/docs/plugins.md#权限) 与共享 Qt 契约：normal 的文件读写仅声明，不是路径沙箱；精确路径声明与最低版本仍需实施验证。`IFileOperation` 在目标需要更高权限时由 Shell 自行弹出提权提示（`FOFX_SHOWELEVATIONPROMPT`），插件不静默降权也不自行提权。

插件仅在用户显式开启 RN-FR-14 后遍历选定根，不读文件内容；元数据只取规则所需字段（RNF-23 限定的 8 种扩展名）。批次清单放宿主提供的数据目录，限制访问并不写入文件内容。诊断不记录完整个人路径，除非用户明确导出并确认。oil 模式的 id 表与缓冲区**只存在于内存与数据目录**，不含文件内容。

AI 首版非核心，不请求 `network`。未来如提出 AI 功能，必须单独明确发送的名称/元数据、服务端、脱敏、同意、取消与密钥存储，经过权限审批。预设只存规则与标志位，不存文件列表。

## 7. 失败矩阵

| 情形 | 检测/风险 | 期望处理 |
|---|---|---|
| 空选择/宿主上下文缺失 | 输入集合未知 | 显示可操作错误，不扫描磁盘 |
| `files` 数组超长度上限 | 只收到部分路径 | 明确提示少选，不静默截断 |
| 正则语法错/灾难性慢匹配 | 无效规则或 UI 卡顿 | 即时标记；核心运行受限/可取消；不执行旧预览 |
| Windows 非法字符/保留设备名/长度超限 | 目标路径非法 | 按 Windows 规则校验，阻断目标并说明字段 |
| 目标冲突、批内重复、外部进程抢占 | 覆盖风险 | 拒绝覆盖（不加 `RENAMEONCOLLISION`）；执行前再次检查 |
| 文件名交换、仅大小写修改 | 中间目标冲突 | 由 `IFileOperation` 处理；实测不支持时明确报错而非静默 |
| 父目录与后代同时改名 | 路径随父目录变化 | 深度倒序：子先父后并修正子树路径；预览提示存在父子关系 |
| Unicode 规范化、组合字符 | 比较与磁盘语义差异 | NFC 归一化后比较（RNF-19）；不擅自改变磁盘上的字节 |
| 名字里含 NBSP | NFC 归一化会把它换成普通空格 | 收尾阶段显式提示"含不换行空格，已按普通空格处理"；不静默 |
| 整串都是空白/点 | 下标边界 | 判为非法名并报错；实现必须避免无符号下标下溢 |
| 执行时用户/其他程序修改或替换文件 | 过期预览/错误对象 | 文件身份变化即停止该项或整批；不改新对象 |
| 用户取消 | 批次部分执行 | 安全边界生效，逐项报告位置；不虚报回滚 |
| 进程崩溃 | 批次部分执行 | 不自动回滚；提示用 Shell 撤销并保留批次清单 |
| `IFileOperation` 提权被拒 | 权限不足 | 显示 Shell 返回原因与逐项状态；不静默重试 |
| SMB 共享断线 | 提交中断 | 明确报告已改名与未改名的项；不自动重放 |
| 时间/元数据不可读 | 日期模板结果不确定 | 保留 `$PATTERN` 字面量并标该项错误；不以当前时间静默替代 |
| **oil：`/<id>` 被改** | 找不到对应条目 | 该行判错误，其余行照常解析；禁止提交 |
| **oil：缓冲区同时有改名与删除/新建** | 超出首版边界 | 删除/新建行判错误并拒绝落盘；不降级执行改名部分 |
| **oil：粘贴整块外部文本** | 无 `/<id>` 前缀 | 全部判为新行 → `首版不支持新建`；提示用控件模式批量生成 |
| 未来 AI 服务无响应/输出冲突 | 外发失败/不安全名字 | 首版不依赖 AI；未来仅作为待审预览，仍走本地校验与确认 |

## 8. 交付清单

本次仅八份中英文文档；不创建 manifest、源码、Qt/CMake 配置、依赖清单、测试脚本或 Explorer 注册项。获批实现后补齐真实宿主入口/manifest、第三方依赖与许可证、预设与批次清单格式、版本变更、安装包以及测试证据。

## 9. 测试计划（L0–L5）

逐用例行及 traceability 见 [TEST-PLAN.md](TEST-PLAN.md)。L0 manifest/打包静态校验；L1 纯核心；L2 fake 文件系统/宿主依赖注入；L3 Win顺真实宿主往返（完成门槛）；L4 Windows/SMB 真机交互；L5 标定与发布。L1/L2 已执行（85 条单元用例全绿），L3 的 e2e 部分已执行（14 项真实改名断言全绿），**宿主往返本身未执行**，L4/L5 全部未执行。量化目标是预算，不能代替测量。实测记录见 [实施验证](docs/validation/rename-m0-20261110/IMPLEMENTATION.md)。

## 10. 诚实笔记（Honest notes）

**实测：**读取 day.rename README、核心和 manifest；读取宿主 `plugins.md` 的 `files` 触发、协议与共享 Qt 契约；读取本地 PowerToys 检出 `src/modules/PowerRename/` 的核心（规则函数、标志位、状态枚举、预览线程、`IFileOperation` 提交、深度倒序、收尾三步、NFC 等价、模板引擎、枚举偏移、元数据提取、fuzz 与单测）；读取 oil.nvim 的 README、解析器与 mutator。行号见 §2。

**推断：**native C++/Qt 共享 runtime 可使插件不依赖独立 Python 环境；以宿主 `files` 入口能把所选集合传给重命名工具；`IFileOperation` 的深度倒序足以安全处理父子目录同时改名。这些是架构目标，不是宿主支持事实。

**主动偏离（不是能力缺失）：**
- **不抄 `FOF_RENAMEONCOLLISION`**。PowerRename 用它在冲突时自动改名为 `name (1)`；我们的 RN-FR-05 要求绝不覆盖，冲突即拒绝。
- **不抄 `ItemNameAlreadyExists` 的死代码状态**。PowerRename 定义了这个枚举值但从未赋值（RNF-11），我们把它真正实现。
- **oil 模式首版只做改名**。oil.nvim 支持新建/删除/移动/跨目录，我们刻意收敛到改名，使风险面与控件模式相同。
- **不实现树视图**。照抄 oil.nvim 的判断（RNF-35）：树视图是另一套方法论。
- **不抄 `PowerRenameManager` 的 `PeekMessage` 消息泵**。Qt 下改用 `QEventLoop` + 信号槽。
- **不抄自研两阶段改名 + 恢复日志**。改用 `IFileOperation` + Shell 撤销栈 + 只读批次清单。

**已实现并测过：**规则核心（拆分、收尾三步、四档大小写、日期/计数/元数据模板、规则链、oil 解析与差异、批次冲突检测）、`IFileOperation` 执行器（深度倒序、冲突拒绝、不自动改名）、双模式状态一致性（`Presenter`）。证据：85 条单元用例、14 项真实改名断言，见 [实施验证](docs/validation/rename-m0-20261110/IMPLEMENTATION.md)。

**未验证：**宿主 `files` trigger 契约与最低版本、native 插件模板/API、共享 Qt 契约在本插件上的实际可用性（Qt 窗口代码从未编译）、Qt regex 与 ECMAScript/Boost 的差异、Windows 文件身份与 case-only/交换改名、`IFileOperation` 在 SMB 上的行为、撤销是否真的生效、崩溃后的实际状态、元数据的端到端读取、无障碍与打包。宿主往返（L3）未执行。

## 11. 交付报告（实施后回填）

本轮交付了纯核心、Windows 适配层、界面逻辑与自动化测试，**未发布**：宿主往返（L3）未执行，按本节规则不标记完成。后续按版本记录需求偏差、真实宿主与文件系统矩阵、L0-L5 证据、核心兼容子集、两种模式的实际使用数据、已知限制和未验证边界。
