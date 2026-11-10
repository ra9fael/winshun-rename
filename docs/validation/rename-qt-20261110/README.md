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
