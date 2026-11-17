# ra9fael.rename 本地逻辑验证规范（TEST-SPEC）

**状态：本轮验证的规范，先于实现。** 双语互链：[English](TEST-SPEC.en.md) · [测试计划](TEST-PLAN.md) · [设计](plugin-design.md) · [UI 规格](UI-SPEC.md)。

本文件与 `TEST-PLAN.md` 的区别：TEST-PLAN 覆盖到 L5（真机、SMB、DPI、读屏、发布），是长期账本；**本文件只定义"在还没装进宿主之前，本地必须验证到什么程度"**，即 L1（纯核心）+ L2（可注入的平台边界）+ GUI 冒烟。宿主集成（L3）不在本文件范围内。

## 0. 边界：本轮要证明什么，不证明什么

要证明：

1. **改名决策正确**——规则链、作用范围、收尾三步、冲突检测、非法名判定，都有可重复的本地断言。
2. **两种输入方式等价**——控件模式的规则链与文本模式（oil）的缓冲区编辑，对同一批文件得出同一批目标名，且拒绝同样的非法输入。
3. **目录作为输入集正确**——"在此目录打开界面"把目录的**直接子项**作为输入集（不递归、不跟随链接、去重），而不是把目录本身当成一项。
4. **单个文件改名正确**——搜索栏的最小路径：一个文件进，一个目标名出，冲突拒绝。
5. **GUI 能起来并用同一套核心**——不依赖宿主进程、不依赖协议，直接传路径即可开窗，窗口的预览与执行和 L1 断言一致。

不证明（本轮明确不测，写下来避免误以为测过）：

- 宿主 `files` 触发、`search_files` 工具往返、共享 Qt 契约、提权、撤销栈、SMB、DPI、读屏、发布包。

## 1. 层次与判定

| 层 | 是什么 | 通过标准 | 在哪跑 |
|---|---|---|---|
| **S1 纯核心** | `core/`，无 Windows、无 Qt | `ctest -R rename_core` 全绿 | 任意平台 |
| **S2 平台边界** | `win/` 的枚举与执行，临时目录真实读写 | `ctest -R rename_e2e` 全绿，且清理干净 | Windows |
| **S3 视图模型** | `ui/presenter`，纯 C++ | `ctest -R rename_core`（presenter 用例在同一二进制内）全绿 | 任意平台 |
| **S4 GUI 冒烟** | `rename-ui.exe` 直接开窗 | 窗口出现、行数/状态与 S1 一致、执行后磁盘真变 | Windows + Qt |

**门槛**：S1+S2+S3 全绿才能进入 S4；S4 通过才能谈"装进宿主"。

## 2. 用例

编号 `LS-<层>-<序号>`。每条都必须可执行、有断言、有证据文件；"看着对"不算通过。

### S1 纯核心：名字拆分与收尾

| ID | 步骤 | 预期 |
|---|---|---|
| LS-S1-01 | `Split("archive.tar.gz")` | name=`archive.tar`，extension=`gz` |
| LS-S1-02 | `Split(".gitignore")` / `Split("README")` / `Split("name.")` | 三者都无扩展名；`name.` 的点不算扩展名 |
| LS-S1-03 | `Split` → `Join` 往返 | 逐字节相同 |
| LS-S1-04 | `Portion(Extension)` 于无扩展名项 | 返回空且标记"不适用"，不是返回整名 |
| LS-S1-05 | `Finalize` 尾随空格与点 | 被 trim，且 `trimmed=true` |
| LS-S1-06 | `Finalize` 全空白 / 全点 | `ErrorEmptyName` |
| LS-S1-07 | `Finalize` NBSP | 换成普通空格，`hadNbsp=true` |
| LS-S1-08 | `Finalize` 非法字符 `? * : < > \| "` | `ErrorInvalidCharacter` |
| LS-S1-09 | `Finalize` 仅大小写变化 | 判为**变化**（不是 Unchanged） |
| LS-S1-10 | `ExceedsLength` 以差值判定 | 原路径 250 + 目标叶 +20 → 超长；-20 → 不超 |
| LS-S1-11 | NFC 等价（组合 vs 分解） | `NfcEqual` 真；`ß` vs `ss` 假 |
| LS-S1-12 | 无 normalizer 时 | 逐码元比较，不崩 |

### S2 纯核心：大小写与模板

| ID | 步骤 | 预期 |
|---|---|---|
| LS-S2-01 | Uppercase / Lowercase / Capitalize / Title Case | 四档固定；Title Case 例外词（a/an/the/of…）保持小写；撇号缩写 `don't` → `Don't` |
| LS-S2-02 | 日期模式全集（`$YYYY`…`$fff`、`$TT`、`$tt`、`$$`） | 每个都确定；长模式优先；`$$` 出单个 `$` |
| LS-S2-03 | `$D` 与 `$DATE_TAKEN_YYYY`、`$DESCRIPTION`、`$DOCUMENT_ID` 共存 | 负向前瞻生效，`$D` 不吞长键 |
| LS-S2-04 | `$H` 与 `$HEIGHT` 共存 | 同上 |
| LS-S2-05 | 计数器 `${start,increment,padding}` | start/increment/padding 全生效；大 padding 不爆内存 |
| LS-S2-06 | 计数器只在实际匹配时前进 | 不匹配项不占号；同一项内两个计数器同号 |
| LS-S2-07 | 未知占位符 | 原样保留，不报错也不静默替换 |

### S3 纯核心：规则与计划

| ID | 步骤 | 预期 |
|---|---|---|
| LS-S3-01 | 字面量 first vs all | 只改首个 / 改全部 |
| LS-S3-02 | 字面量默认不区分大小写；加 `FlagCaseSensitive` 后区分 | 两者都可 |
| LS-S3-03 | 正则捕获组替换 | `(\d+)` → `$1` 生效 |
| LS-S3-04 | 正则语法错误 | `ErrorBadRule`，不是崩 |
| LS-S3-05 | scope=Extension 对无扩展名文件与目录 | 都不适用，行标 Unchanged 且带 note |
| LS-S3-06 | 规则链每步重新拆分 | `a.b.js`：先 name 改 a，再 extension 改 js，结果仍是 `a.b.new` |
| LS-S3-07 | 禁用规则 | 完全跳过 |
| LS-S3-08 | 批内两条目标同名 | 两条都标 `ErrorAlreadyExists`（不是只标一条） |
| LS-S3-09 | 目标已存在于磁盘 | `ErrorAlreadyExists` |
| LS-S3-10 | 无规则匹配 | `Unchanged` |

### S4 纯核心：oil 解析与差异

| ID | 步骤 | 预期 |
|---|---|---|
| LS-S4-01 | 正常缓冲区（含缩进、目录尾斜杠） | 缩进丢弃；目录标记正确 |
| LS-S4-02 | 同 id 换名 | 产一条 Rename |
| LS-S4-03 | 行顺序变化 | 不产差异 |
| LS-S4-04 | 改 `/` 后的 id | 该行 `UnknownId`，其余行照常 |
| LS-S4-05 | 行消失 | `VanishedId` → 判为删除，本轮拒绝 |
| LS-S4-06 | 无锚点新行 | 判为新建，本轮拒绝 |
| LS-S4-07 | ` -> ` 链接语法 | 判为链接，本轮拒绝 |
| LS-S4-08 | 批内大小写不同的同名 | 按 FS 语义归一后判重复 |
| LS-S4-09 | 解析错误行 | 有行号与原因；一行坏不影响其他行 |
| LS-S4-10 | CRLF 与空行 | 容忍 |

### S5 两种模式等价

| ID | 步骤 | 预期 |
|---|---|---|
| LS-S5-01 | 同一批文件：控件模式规则 vs oil 缓冲区 | 目标名逐项相同 |
| LS-S5-02 | 同一非法名（`bad?.txt`）两种模式 | 都 `ErrorInvalidCharacter` |

### S6 视图模型

| ID | 步骤 | 预期 |
|---|---|---|
| LS-S6-01 | 空输入 | `Phase::Idle`，`BlockingReason()=="no-files"` |
| LS-S6-02 | 过滤器切换 | 可见行变，**输入集不变**；执行顺序取自计划不是可见行 |
| LS-S6-03 | 取消勾选某行 | 计划尺寸不变，执行顺序少一项 |
| LS-S6-04 | Rebuild 后 | 选中状态保留（按 id） |
| LS-S6-05 | 过期 planId | `AcceptPlan` 拒绝 |
| LS-S6-06 | 批内冲突 | `CanExecute()==false`，`BlockingReason()=="errors"` |
| LS-S6-07 | 深度优先执行顺序 | 孙 → 子 → 父 |
| LS-S6-08 | 执行后 | 缓冲区反映新名，`Phase::Idle` |
| LS-S6-09 | oil 缓冲区复制→粘贴 | 往返字节相同，状态全 Unchanged |
| LS-S6-10 | 有未提交改动时切模式 | `CanSwitchMode` 为假 |

### S7 目录作为输入集（本轮新增）

| ID | 步骤 | 预期 |
|---|---|---|
| LS-S7-01 | `Enumerate{roots:[目录], listContents:true}` | 返回**直接子项**，不含目录自身；深度全为 0 |
| LS-S7-02 | 同上但目录为空 | 返回空、不报错 |
| LS-S7-03 | 目录含子目录 | 子目录作为一项（`isFolder=true`），其内容**不**列出 |
| LS-S7-04 | 目录含 junction/符号链接 | 链接本身列出，但不进入；有 note |
| LS-S7-05 | roots 同时给目录与其中某文件 | 去重，该文件只出现一次 |
| LS-S7-06 | `listContents:false` 且 roots 是目录 | 目录自身作为一项（原行为不变） |
| LS-S7-07 | 不存在的根 | `blocked=true` 且有原因 |
| LS-S7-08 | 系统盘根 `C:\` | 列出直接子项，不递归 |
| LS-S7-09 | 对目录内容执行改名 | 文件真的改名；子目录改名时其内容路径不受影响（深度全 0，无父子关系） |

### S8 单文件最简路径（本轮新增）

| ID | 步骤 | 预期 |
|---|---|---|
| LS-S8-01 | 单个文件 + `find→replace` | 一行 `Ready`，目标名正确 |
| LS-S8-02 | 单个文件 + 无替换词 | `Unchanged`，并提示补替换词（不是把查找词删掉） |
| LS-S8-03 | 单个文件，目标已存在 | `ErrorAlreadyExists`，`CanExecute()==false` |
| LS-S8-04 | 单个文件执行 | 磁盘上真改名，Shell 撤销可用 |
| LS-S8-05 | 单个文件，名字里查找词不出现 | `Unchanged` |

### S9 GUI 冒烟（本地，不经宿主）

| ID | 步骤 | 预期 |
|---|---|---|
| LS-S9-01 | `rename-ui.exe --dir <目录>` | 窗口出现，行数 = 目录直接子项数 |
| LS-S9-02 | 同上，规则填 `a→b` 并回车 | 预览行变"待改"，计数正确 |
| LS-S9-03 | 点"重命名 N 项" | 磁盘真变；窗口显示完成行 |
| LS-S9-04 | `rename-ui.exe --files <文件1> <文件2>` | 两行，顺序与参数一致 |
| LS-S9-05 | 文本模式切换 | 缓冲区出现 `/<id> 名字`，行数与预览一致 |
| LS-S9-06 | 深浅色 / 界面大小 | 换肤生效（用已有的 `RENAME_GRAB_WINDOW` 截图钩子取证） |

### S10 宿主往返：行携带的 `data`（本轮新增）

这是**协议契约**用例，不是业务用例。宿主把 item 原样回传（`Items.cpp::itemToJson`），插件放在 `data` 里的字段就以 `item.data.*` 回来；`action()` 只看顶层，就会对一行明明带着 batch 的结果回答"没有选中文件"。这一条不可能被纯核心测试覆盖，只能靠协议往返。

| ID | 步骤 | 预期 |
|---|---|---|
| LS-S10-01 | 宿主形态的 item（`{data:{openFolder:…}}`）走 `action` | 生效（`hide`，或在无窗口构建里说明原因）；**不得**是"没有选中文件" |
| LS-S10-02 | 宿主形态的 item（`{data:{batch:[…]}}`）走 `action` | 报出"已重命名 N 项"，且磁盘真的改名 |
| LS-S10-03 | 顶层直接给 `batch`（旧形态/once 插件路径） | 仍然生效（兼容两种形状） |
| LS-S10-04 | `items[]` 里每行的 `data` 与 `title`/`subtitle` | 与 `query()` 发出的一致，没有被宿主改写 |

## 3. 证据与留痕

每次执行都要落文件，放在 `docs/validation/<主题>-<日期>/`：

- `ctest-<层>.txt`：`ctest --output-on-failure` 的原始输出
- `s7-directory.txt`：S7 与 S8 的断言输出（`rename_tests --filter` 或独立二进制）
- `host-sim-*.txt`：S10 的协议往返输出（`tools/host-sim.ps1`）
- `gui-<场景>.png`：S9 的窗口渲染图（`RENAME_GRAB_WINDOW`）
- `NOTES.md`：本轮"未测量"清单

**没有证据文件的用例一律记为"未执行"**，不得在文档里写成通过。

## 4. 失败时的处理

1. 先判断是实现错还是规范错。规范错就改本文件（并注明日期与原因），不要为了迁就实现改预期。
2. 实现错就修，修完重跑**该层及以下所有层**（S1 改动会波及 S2…S6）。
3. 修不掉的，把用例标 `未通过` 并写清现象；不许跳过。

## 5. 与宿主的边界（本轮不做，写下来对齐）

插件侧的搜索栏只保留两件事：**单文件最简改名**、**一行"在此目录打开重命名界面"**。批量与 oil 全部在窗口里完成。等 S1–S9 全绿，再决定 manifest、`files` 触发、共享 Qt 与打包。
