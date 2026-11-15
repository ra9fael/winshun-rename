# Rename（Proposal）

**状态：核心与窗口已在本地验证（S1–S9），宿主未验证。** 本地验证规范见 [TEST-SPEC.md](TEST-SPEC.md)，本轮实测记录见 [docs/validation/rename-local-20261113/](docs/validation/rename-local-20261113/README.md)。语言/文档互链：[English](README.en.md) · [设计与需求](plugin-design.md) · [界面规格](UI-SPEC.md) · [测试计划](TEST-PLAN.md)。

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
- **文本模式首版只做改名**。oil.nvim 支持新建/删除/移动/跨目录，我们收敛到改名，使风险面与控件模式完全相同。
- **不实现树视图**。照抄 oil.nvim 自己的判断：树视图是另一套方法论。
- **不写自研恢复日志**。改名与撤销都交给 Shell，崩溃后不自动回滚，只保留只读批次清单做诊断。

## 建议能力

规则作用范围：名称、扩展名、完整名称；匹配首个或全部；计数模板和日期模板；元数据模板（EXIF/XMP，限 8 种扩展名）；大小写变换四档；预览匹配高亮、选择、过滤、排序；规则链和预设。

执行设计明确处理取消、交换名、父子目录组合、仅大小写变更、Unicode 与 NFD、Windows 保留名、执行期间外部修改及 SMB 行为。撤销走 Shell 撤销栈，撤销前重新核对文件身份，不覆盖后来创建的文件。首版 AI 不作为核心功能。递归收集为 P1 可选项、默认关闭，显式开启后仅收集选定根下文件、不自动改目录。文件仅最后后缀为 extension（`archive.tar.gz` 为 `gz`），`.gitignore` 无后缀；无后缀 extension 规则默认跳过并解释，目录 extension 不适用。尾部空白与点按收尾规则去除，但预览显式提示，不静默。

## 文档索引

- [设计与 11 节需求](plugin-design.md)：事实（含两份借鉴对象的逐条来源）、双模式架构、需求 ID、权限、失败矩阵、诚实笔记。
- [UI 规格](UI-SPEC.md)：两种模式各一张 ASCII 线框、布局、字段、快捷键、错误态、主题/缩放与无障碍。
- [测试计划](TEST-PLAN.md)：逐行 ID、FR、L0-L5、前置、步骤、预期、证据、未执行状态；含"与借鉴对象的行为差异"防错清单。
- [本地验证规范](TEST-SPEC.md)：装进宿主之前本地必须验证到什么程度（S1–S9），含目录输入集与单文件路径的断言；先于实现。

核心、平台边界与窗口已在本地通过（`ctest` 3/3、`rename_e2e` 36/36、S9 六张渲染图）。宿主集成、真机交互与发布仍是未验证目标。
