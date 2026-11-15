# Vim 模式

文本模式（oil 模式）可以启用 Vim 按键。功能本身由独立的可移植库
[`winshun-vimcore`](https://github.com/ra9fael/winshun-vimcore) 提供，本仓库只放 Qt 适配层。

## 组成

| 位置 | 内容 | 依赖 |
|---|---|---|
| `vendor/vimcore`（submodule） | 模式机、motion、operator、寄存器、撤销/重做、Replace、数字操作、`.` 重放，126 个测试 | 纯 C++20，无 Qt、无 Win32 |
| `ui/vim_qt.{h,cpp}` | `vimcore::Document` 的 `QPlainTextEdit` 实现、按键翻译、方块游标、事件过滤器 | Qt Widgets |
| `ui/widgets_window.*` | 文本页的 `Vim` 开关、`-- NORMAL --` 徽标、当前行预览高亮；Esc 不抢 Vim 编辑器的按键 | — |
| `ui/bridge.*` | `vimEnabled()` / `setVimEnabled()`，存 `<dataDir>/settings.json` | — |
| `tests/test_vim_qt.cpp` | 用真实 `QKeyEvent` 验证适配层，25 个测试 | Qt Widgets（offscreen） |
| `tests/test_vim_window.cpp` | 窗口级回归：Esc、插入同步、R/Ctrl-A、实时预览，14 个测试 | Qt Widgets（offscreen） |

## 构建

Vim 模式只进 Qt 构建。submodule 缺失时 CMake 会失败并提示：

```bash
git submodule update --init --recursive
cmake -B build-qt -S . -DRENAME_BUILD_QT=ON
cmake --build build-qt --parallel
ctest --test-dir build-qt --output-on-failure   # vim_qt、vim_window 与 rename_* 共 6 个目标
```

`vendor/vimcore` 的测试默认关闭（`VIMCORE_BUILD_TESTS=OFF`），单独验证库时：

```bash
cmake -B build -S vendor/vimcore && cmake --build build && ctest --test-dir build
```

## 行为约定

- **默认关闭**，用户点 `Vim` 才启用；选择写入 `<dataDir>/settings.json` 的
  `{"vimMode":true}`，下次启动恢复。
- **插入模式保持原生输入**：普通按键、IME、粘贴、拖放由 `QPlainTextEdit` 处理；
  只拦截 Esc。原生文本变化会刷新现有 Vim 核心，保留 Insert 状态与该会话的撤销快照，
  后续 Normal 操作不会基于过期文本计算。
- **Replace 模式独立处理**：`R` 逐字符覆盖，Backspace 恢复原字符；因为原生控件默认是插入，
  Replace 模式由核心接管按键并暂时关闭输入法。
- **Esc 归当前焦点所有**：Vim 开启且文本编辑器有焦点时，Esc 由 Vim 处理（Insert 退出到
  Normal）；其他焦点或 Vim 关闭时，Esc 关闭窗口。它不是窗口级 `QShortcut`，避免抢先拦截。
- **窗口快捷键保留**：Ctrl+S 执行、Ctrl+Z/Y 仍是宿主撤销、Alt 与 F 键放行；
  Vim 消费 Ctrl+R 重做、Ctrl+A/X 数字增减、Ctrl+D/U/F/B 翻页。
- **预览跟随 Vim 光标**：右侧表格实时重算文本缓冲区的目标名，并高亮当前光标所在的 `/id` 行；
  普通模式的改动经 120ms 缓冲同步后即可看到目标名变化。
- **撤销归核心所有**：`u` / Ctrl+R 走 vimcore 自己的快照，支持 count，一次插入或 Replace 会话算一步，
  不影响控件自身的撤销栈；宿主 Ctrl+Z 改动后核心也会重新同步。
- **支持的扩展操作**：`R`、Ctrl+A/X 数字增减、Ctrl+D/U/F/B 翻页、Visual `o`/`u`/`U`/`~`/`p`/`J`、
  `iw`/`aw` 文本对象、`%` 同行括号匹配、`.` 重放上次修改。

## 0.2 支持范围

Normal / Insert / Replace / Visual / Visual Line；`h j k l`（不跨行）与 Space/Backspace
（跨行）、`w b e 0 ^ $ gg G f F t T %`；`i a I A o O s R`；
`d c y` 配 motion、文本对象（`iw` / `aw`）与叠用（`dd cc yy`）、`D C S x X Y p P J r ~`；
Ctrl+A/X 数字增减、Ctrl+D/U/F/B 与 PageUp/PageDown 翻页；计数前缀、无名寄存器、
`;` / `,` 重复查找与 `.` 重放上次修改。

不做（有意）：ex 命令行、命名寄存器、宏、marks、`/` `?` 搜索、`g` 前缀（`gg` 除外）、
多缓冲区。新增按“小而带测试”的增量来加。
