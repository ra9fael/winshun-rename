# ra9fael.rename UI 规格（Proposal）

**状态：规格；界面逻辑已实现并测过，Qt 渲染未验证。** 中英互链：[English](UI-SPEC.en.md) · [README](README.md) · [设计](plugin-design.md) · [测试计划](TEST-PLAN.md) · [实施验证](../WinShun/docs/validation/rename-m0-20261110/IMPLEMENTATION.md)。以下线框使用 ASCII；控件标识为提案，待真实宿主核实。

## 1. 入口与主布局

入口为宿主针对已选文件结果提供的菜单/动作 `rename.open`，依赖宿主 `files` 触发契约（设计 RNF-07）。不得将其写成已存在的 Explorer 右键集成。Explorer 集成另列探索阶段。

窗口顶部是**模式切换**：`[控件模式] [文本模式]`。两种模式共用同一份选中集合、同一套收尾逻辑与同一个执行器（RN-FR-20）。切换时若另一模式有未提交编辑，先弹确认。

稳定 ID：`rename.mode-control`、`rename.mode-text`、`rename.input-summary`、`rename.rule-list`、`rename.rule-scope`、`rename.match-mode`、`rename.flags-panel`、`rename.preview-table`、`rename.preview-filter`、`rename.oil-editor`、`rename.oil-error-count`、`rename.execute`、`rename.cancel`、`rename.undo`。

## 2. 控件模式

```text
+--------------------------------------------------------------------------+
| Rename   24 selected                    [控件模式] [文本模式]  [Reset]    |
+--------------------------------------------------------------------------+
| RULE 1  [Enabled] [Scope: Name v] [Literal v] [First match v] [Remove]   |
| Find: [draft            ]  Replace: [final             ]  [Add rule]      |
| Rule 2  [Enabled] [Scope: Extension v] [Regex v] [All matches v]         |
| Find: [^JPG$            ]  Replace: [jpg               ]                 |
+--------------------------------------------------------------------------+
| FLAGS  [x] RegEx  [ ] Case  [x] All matches  [x] Enumerate               |
|        Name only [ ]   Extension only [ ]   Randomize [ ]                |
|        Case: [Uppercase][Lowercase][Title Case][Capitalize]              |
|        Skip: [x] Files [x] Folders [ ] Subfolders                        |
+--------------------------------------------------------------------------+
| PREVIEW  24 items   [Selected v] [Filter matches...      ] [Sort: Name v] |
| [x] Original name         Match                 New name          Status  |
| [x] draft (1).txt         [draft]               final (1).txt     Ready   |
| [x] notes.txt             (no match)            notes.txt         Unchanged|
| [ ] draft?.txt            [draft]               Invalid name      Error   |
+--------------------------------------------------------------------------+
| 22 changes  1 unchanged  1 error      [Cancel] [Rename selected: 22]      |
+--------------------------------------------------------------------------+
```

### 2.1 规则行

每条规则包含：启用复选框、scope（Name / Extension / Full name）、literal/regex、first/all、find、replace、移除按钮。规则顺序可键盘调整（提供可见命令，不能只靠拖拽）。规则间关系明确为"上一步输出是下一步输入"。

### 2.2 标志位面板（设计 §3.4）

照抄 PowerRename 的控件组合，按首版裁剪：

- **匹配**：`RegEx`、`Case sensitive`、`All matches`
- **作用域快捷**：`Name only`、`Extension only`（与规则行的 scope 互斥，切换时同步规则行）
- **大小写变换**（四选一，RN-FR-17）：`Uppercase`、`Lowercase`、`Title Case`、`Capitalize`
- **排除**：`Skip files`、`Skip folders`、`Skip subfolder contents`
- **模板**：`Enumerate`（计数模板）
- 首版**不提供** `Randomize`（设计已裁掉，理由是不可复现）

### 2.3 预览表与过滤（RN-FR-10）

预览列：选择、原名、命中高亮、目标名、状态/错误。

三个正交状态维度：

| 维度 | 取值 | 影响 |
|---|---|---|
| 状态 | Ready / Unchanged / Error / TooLong / Exists | 能否执行 |
| 可见性 | 由过滤器决定 | 只见显示 |
| 选中 | 勾选框 | 是否执行 |

表头点击切换过滤器：`All` → `Selected` → `Applicable` → `To rename` → `All`。过滤只改变可见行，**不改变输入集合**。

排序列出当前依据；规则执行顺序与计数顺序有明确标识。

### 2.4 状态与授权

空态：`No files selected` 并提示从宿主选择文件；不显示虚构文件。规则无匹配时仍显示原名与 `Unchanged`。

正则错误标记规则与影响行，旧计划立即失效。截断提示：`files` 数组超长度上限时显示"仅收到前 N 项，请少选"，不静默截断。

执行确认列出有效数量、冲突/错误、目标摘要、递归开关状态、收集根目录与文件数量。父目录与后代同时改名时，预览明确标注父子关系并说明执行顺序（子先父后），不阻断。

执行中逐项显示阶段、当前项、成功/失败；取消后展示已完成与未完成数量。成功后显示"Undo this batch"；Undo 前重新验证文件身份与原名占用，冲突时拒绝，不覆盖后来文件。

### 2.5 收尾提示（设计 §3.5 / §3.6）

尾部空白或点被去除时，预览行显示 `已去除尾部空格/点` 的次级提示，不静默。名字含 NBSP 时显示 `含不换行空格，已按普通空格处理`。

## 3. 文本模式（oil 模式，RN-FR-18 / RN-FR-19）

```text
+--------------------------------------------------------------------------+
| Rename   24 selected                    [控件模式] [文本模式]  [Reset]    |
+--------------------------------------------------------------------------+
| Pre-check [x]   24 entries · 22 renames · 2 errors    [Apply: 22]  [Undo] |
+-----+--------------------------------------------------------------------+
|  1  | /1042 report-draft.txt                                             |
|  2  | /1043 report-final.txt                                             |
|  3  | /1044 photos/                                                      |
|  4  |       /1045 IMG_0001.jpg                                           |
|  5  | /1046 notes.txt                                                    |
|  6  | /1047 draft?.txt          <- 非法字符，标红                          |
|  7  | /1048 missing.txt         <- 目标已存在，标红                        |
+-----+--------------------------------------------------------------------+
| 2 errors; cannot save.                                    [Cancel]       |
+--------------------------------------------------------------------------+
```

### 3.1 编辑器

- 等宽字体，左侧行号槽。
- `/<id>` 前缀以次级色渲染且**不可编辑**（光标跳不过去）；叶子名可自由编辑。
- 缩进只为视觉层次，解析时丢弃（设计 RNF-36）；**不提供折叠或树视图**（RNF-35）。
- 粘贴整块外部文本时，无 `/<id>` 前缀的行全部判为新建 → 错误（RN-FR-19），底部提示"文本模式首版仅支持改名；批量生成请用控件模式"。

### 3.2 预检与错误显示

- 输入停止 300 ms 后后台预检（可关闭）。预检不落盘、不改 id 表。
- 错误行整行浅红底 + 叶子名下方波浪线；行尾行内注释式提示（`<- 目标已存在`）。
- 底部固定显示 `N entries · M renames · K errors`。
- 存在任一错误时 `Apply` 按钮禁用，文案固定为 `N errors; cannot save.`

### 3.3 提交流程

`Ctrl+S` 或点 `Apply` → 阻断检查（错误行 / 目标已存在 / 缓冲区同时有改名与删除）→ 确认弹窗：

```text
+---------------------------------------------+
| Apply renames?                              |
|   22 rename   0 create   0 delete   0 error |
|   3 parent/child pairs will rename children |
|   first                                      |
|                        [Cancel]  [Rename 22] |
+---------------------------------------------+
```

执行时显示进度窗口（照抄 RNF-34 的做法）：逐条推进、可取消、取消后逐项报告位置。

完成后缓冲区重置为新的 `/<id>` 行，id 重新分配，id 表同步更新（否则第二次编辑会指向旧名）。

### 3.4 与控件模式的切换

切换前若当前模式有未提交编辑，弹确认：`丢弃文本模式的未提交编辑？` 选择保留则取消切换。切换保留选中集合与递归开关。

## 4. 键盘、可访问性、主题与缩放

### 4.1 控件模式焦点顺序

输入摘要 → 模式切换 → 规则行 → 规则字段 → 标志位面板 → 预览过滤/排序 → 结果行 → 执行操作。Tab/Shift+Tab 导航，Space 切换启用/选择，方向键移动表格行。Enter 不得绕过执行确认；Esc 关闭非破坏性弹窗。

### 4.2 文本模式键盘

`Ctrl+S` 提交、`Ctrl+Z`/`Ctrl+Y` 撤销重做编辑、`Ctrl+A` 全选、方向键/Home/End 常规移动。`Home` 跳到叶子名起始（跳过不可编辑的 `/<id>`），`Ctrl+Home` 到行首。Tab 在行号槽与编辑器之间不移动焦点（行号槽不可聚焦）。

### 4.3 可访问性

命中片段通过高亮和文字状态共同表达；错误含图标/文字/可访问描述。颜色不能单独表示状态；屏幕阅读器需读取行原名、目标名、状态与错误。文本模式的错误行必须有不依赖颜色的可读描述（行尾文字提示已满足）。

焦点可见、控件有标签。遵循宿主浅色/深色/高对比主题，不硬编码主题色。100%-200% DPI、Qt 字体缩放与多语言长字符串需验证。

规则长模板换行/水平滚动，不裁掉关键字段；窄窗时规则编辑器纵向排列，预览允许横向滚动，底部执行/取消始终可达。文本模式窄窗时横向滚动，不折行（折行会破坏"一行一条目"的解析直觉）。

## 5. 未验证

没有 Qt 界面、真实宿主入口、截图或键盘/读屏验证。宿主菜单入口、窗口尺寸约束、scope/匹配控件、辅助技术播报、主题 token、DPI/缩放、文本模式等宽字体度量与错误高亮的实际呈现均未验证。`IFileOperation` 提权提示在插件进程中的实际表现未验证。
