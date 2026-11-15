# ra9fael.rename 0.1.0 — Qt 窗口与主题跟随验证

日期：2026-11-10
构建：MSVC 19.51（VS 18 Community），Qt 6.12.0（宿主运行时，`C:\Program Files\WinShun`），Ninja，CMake 3.29

## 验证范围

本轮要回答三个问题：

1. Qt 层（bridge.cpp / qt_host.cpp / Main.qml / Tokens.qml）能否编译、能否真的显示窗口？
2. 窗口外观能否跟随 Win顺 的界面风格（`appearance.nativeLook`），三种界面风格 × 深浅色 × 圆角/直角 × 界面大小是否都生效？
3. 打包出的 `.wsplugin` 在宿主 Qt 运行时下能否从零启动？

## 实测结果

### 编译与测试

- `cmake --build build-qt --parallel`（`RENAME_BUILD_QT=ON`）：0 错误 0 警告（/W4 /permissive-）。
- `ctest --test-dir build-qt`：3/3 通过（rename_core 85 用例、rename_e2e 14 断言、core_purity）。
- `ctest --test-dir build-msvc`（无 Qt 回归）：3/3 通过；headless 协议冒烟 initialize/action/shutdown 正常。

### 窗口与主题跟随（全部实测，证据为窗口自身渲染的 PNG）

证据在 `grab-*.png`。抓图方式：`RENAME_GRAB_WINDOW=<path>` 诊断钩子，`QQuickWindow::grabWindow()` 走场景图，与用户看到的渲染路径一致。

| 证据 | 场景 | 结果 |
|---|---|---|
| grab.png | dark + fluent + 圆角 + standard | 深色底、蓝色 accent，状态"没有要改的名字" |
| grab-rule.png | 同上 + 规则 photo→holiday | 3 行变"待改"，主按钮"重命名 3 项"激活 |
| grab-light.png | light + brand + 圆角 | 跟随 brand 红棕 accent，浅色底 |
| grab-roomy.png | light + fluent + 直角 + roomy + en | 直角生效（rounded=false 归零全部圆角），英文界面，行高变大 |
| grab-switched.png | dark 初始化 → `appearance` 通知切 light | 同一进程内窗口换肤成功 |
| grab-text2.png | 切到文本模式 | oil 缓冲区 `/1 photo_1.jpg` `/2 photo_2.jpg` 正确生成 |
| grab-apply2.png | 规则 + 执行 | 表格清空，显示"已重命名 3 项"；磁盘上 3 个文件真的改名（见下） |

### 真实执行链路

`RENAME_GRAB_RULE=photo\nholiday` + `RENAME_GRAB_APPLY=1`，对临时目录里的 `photo_1..3.jpg` 执行 UI 的"重命名"按钮路径。磁盘结果：

```
holiday_1.jpg  holiday_2.jpg  holiday_3.jpg  batch-20261110-201126.txt
```

batch 记录（JournalLite）：

```
renamed	...\photo_1.jpg	...\holiday_1.jpg
renamed	...\photo_2.jpg	...\holiday_2.jpg
renamed	...\photo_3.jpg	...\holiday_3.jpg
```

IFileOperation 全部成功，Shell 撤销栈可用（FOF_ALLOWUNDO + FOFX_ADDUNDORECORD）。

### 打包与安装路径

- `tools\pack.ps1` → `dist\ra9fael.rename-0.1.0.wsplugin`（zip，根目录 plugin.json + rename.exe，符合 `WinShun/src/plugin/Install.cpp` 的解包约定）。
- sha256: `e2a92fd601ba6c9676080ce39939b5bce70ff174e94b46900c719f6646c42962`（191,326 字节）。
- 包内 exe 经 dumpbin 验证导入 Qt6Core/Gui/Qml/Quick/QuickControls2 —— 正好在共享 Qt 契约保证的模块清单内（docs/plugins.md "原生插件的共享 Qt 契约"）。
- 解包到临时目录、只设宿主 runner 会设的环境变量（`PATH` 前置宿主目录、`WS_QT_DIR`、`QT_PLUGIN_PATH`、`QT_QPA_PLATFORM_PLUGIN_PATH`、`QML_IMPORT_PATH`、`QT_QUICK_CONTROLS_STYLE=Basic`，全部来自 `WinShun/src/plugin/runner/Sandbox.cpp` `withHostQt`）→ 窗口正常启动并渲染（grab-installed.png）。

## 本轮发现并修复的问题

1. **CRT 混链（LNK2038）**：Qt 构建要求全目标动态 CRT，但 core/win/ui 原为静态。CMake 顶层用 `RENAME_CRT` 变量统一，`RENAME_BUILD_QT` 决定它。
2. **`Theme.qml` 文件名遮蔽**：与 Main.qml 同目录的 QML 文件被隐式导入为类型，`Theme` 上下文属性被同名类型遮蔽，所有 `Theme.xxx` 读成 undefined。文件改名 `Tokens.qml`，上下文属性仍叫 `Theme`。
3. **`BlockingReason()` / note 返回英文散文**：改为稳定标识符（`no-files` / `errors` / `nothing-to-rename`；note 为 `no-extension` / `trimmed` / `nbsp`），QML 层映射语言。测试断言同步改为标识符。
4. **文本模式空缓冲区**：首次切入 text 模式时 bridge 从 `m_collected` 构建 oil 表（`/<id> <名字>` 行），用户从宿主给的选择开始编辑，而不是面对空编辑器。
5. **执行后空表误报"没有文件"**：加 `doneLine`（"已重命名 N 项"），空表区分"完成"与"从未选择"。
6. **窗口进程结构**：原设计是独立的 `rename_window.exe`，但宿主只启动 `runtime.program` 指定的一个 exe——窗口永远到不了。窗口并入 rename.exe（Qt 事件循环占主线程、协议循环进工作线程，与 ra9fael.envhosts 同构），无 Qt 构建保持 headless 可测。

## 未测量 / 未验证（诚实边界）

- **`appearanceChanged` 只用 JSON 通知验证过**：宿主真实换肤（用户在设置里点"深色/浅色"）没有在真实宿主里试过，但通知格式取自 docs/plugins.md 的示例逐字。
- **`files` 触发未在真实宿主验证**：窗口路径的输入来自手写的协议行，与 M0 探针的同一个假设。P1 探针日志仍在（`[rename] files trigger: count=N chars=M`），装进宿主后第一条 stderr 就是答案。
- **共享 Qt 的 runner 环境是复刻而非真实**：环境变量集合抄自宿主源码 `Sandbox.cpp`，但宿主 runner 实际启动插件的路径没有跑过。
- **`uiSize: dense/compact` 未抓图**：roomy 验证了缩放路径（level 索引生效），compact/dense 走同一段代码。
- **liquid / tactile 风格未抓图**：token 名相同（Tokens.qml 的 pick 只是按键取值），风格差异全在宿主发的值里。
- **D3D 渲染下的桌面截图全黑**：BitBlt/PrintWindow 对 D3D 合成窗口都拿不到内容，所以诊断钩子走 `grabWindow()`。真实 GPU 窗口的视觉检查仍是"未测量"。
- **undo（在资源管理器里 Ctrl+Z）未手动按过**：FOF 标志在，但"Shell 撤销真的恢复"没有实测。
- **文本模式的"切换确认"对话框**：HasUncommittedEdits 的路径（改了缓冲区再切规则）未在窗口里点过，presenter 层有单测。

## 下一步

装进宿主（设置 → 插件 → 从压缩包安装 → 选 `dist\ra9fael.rename-0.1.0.wsplugin`），选中文件 → 右键"重命名…"，看 plugins.log 里 P1 探针行与窗口是否同时出现。

## 追加（2026-11-11 01:25）：宿主里"一直在崩溃"的根因与修复

现象：装进宿主后 plugins.log 反复出现 initialize 没有回应 → 挂死后重启。

根因（实测）：窗口在 main() 里、serve() 之前同步构建——QGuiApplication + QQmlEngine + 加载 Main.qml 在本机要 **1600–1850 ms**，超过清单的 	imeoutMs: 1500，宿主在插件回答 initialize 前就判挂死。手动复刻宿主环境（Job 内存限制、宿主 Qt 环境变量、完整 initialize 参数）测得 initialize 回应 1650 ms；拆分后确认耗时全部在 QML 场景构建。

修复：Start() 只建 QGuiApplication（毫秒级），QML 引擎与场景延迟到第一次 iles 触发的 Show() 里建（packages 插件同款懒加载）。实测 initialize 回应降到 **66–261 ms**，窗口链路（触发 → 建场景 → 显示 → 预览）复测通过（grab-lazy.png）。

部署：新 rename.exe 已覆盖到 %LOCALAPPDATA%\WinShun\plugins\ra9fael.rename\，包 dist\ra9fael.rename-0.1.0.wsplugin 同步重打（sha256 d039fc3a…bd3a2c）。

教训：常驻插件的 initialize 回应预算是 	imeoutMs，任何重资产（QML 引擎、窗口、缓存）都必须在回答之后、第一次用到时再建。

## 追加（2026-11-11 02:35）：交互重做——关键字即预览

用户反馈：ename 只回一行提示、真入口藏在文件右键菜单里，交互太绕。

新交互（三入口，全部实测）：
1. **ename <查找> -> <替换>**：query 里直接调宿主 search_files 工具（新加 winshun:search 权限），把匹配文件逐行列出（原名 → 新名），第一行是"重命名 N 项"，Enter 执行整批。协议级实测：ename report -> 2026_ 对 3 个文件返回 3 行预览 + batch 数据，action 落盘成功（2026_1..3.docx）。
2. **ename 单独输入**：回用法行（含示例）。
3. **文件右键菜单 → 重命名…**：保留，开完整窗口（双模式）。

实现要点：pattern 解析支持 -> 与 →、两侧空格与全角空格；规则走与窗口同一个 Presenter/Plan；batch 数据随行 data 回传，action 端重新执行（磁盘是唯一真相）。只给查找不给替换时行标"不变"，不猜删除。

包重打：sha256 1f243e49…53ea79（206,180 字节），已同步部署到已安装位置。

## 追加（2026-11-11 02:52）：结果列表交互定稿（用户反馈第二轮）

用户看第一版截图：每行有'重命名'按钮但 Enter 是全部执行，title 只显原名看不出变化——语义混乱。

查宿主源码定边界：插件行的 title 由宿主按搜索词生成高亮（ResultModel NameHtmlRole），插件只能给纯文本；subtitle 恒为纯文本。方案 A 的红/绿行内 diff 在宿主行里画不出来。

定稿（已实现并协议实测）：
- title = 新名字（ready 时）/ 原名（其余），宿主自动高亮输入词
- subtitle = '← 原名'（方向： became from）；不变时'不变 · 没有替换词'；冲突/超长等逐状态说明
- 批执行行是唯一带蓝色标签的行：ready>0 时'重命名 N 项 / Enter 执行全部 N 项 · 可在资源管理器撤消'；ready=0 时'没有要改的名字 / 补上替换词：rename X -> 新名字'
- 无替换词时不建规则（空 replace 会把查找词删掉，违背直觉）——行标不变，提示补替换词
- 去掉每行按钮；文件行不带任何动作

三场景协议实测：带替换（含冲突行）、无替换（全不变+提示）、裸 rename（用法行）。ctest 3/3。包 sha256 63119ab3…c6d49；已装位置被运行中的宿主占用，等宿主重载插件后生效。
