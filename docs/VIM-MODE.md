# Vim 模式

文本模式（oil 模式）可以启用 Vim 按键。功能本身由独立的可移植库
[`winshun-vimcore`](https://github.com/ra9fael/winshun-vimcore) 提供，本仓库只放 Qt 适配层。

## 组成

| 位置 | 内容 | 依赖 |
|---|---|---|
| `vendor/vimcore`（submodule） | 模式机、motion、operator、寄存器、撤销/重做，57 个测试 | 纯 C++20，无 Qt、无 Win32 |
| `ui/vim_qt.{h,cpp}` | `vimcore::Document` 的 `QPlainTextEdit` 实现、按键翻译、事件过滤器 | Qt Widgets |
| `ui/widgets_window.*` | 文本页的 `Vim` 开关和 `-- NORMAL --` 徽标；Esc 不抢 Vim 编辑器的按键 | — |
| `ui/bridge.*` | `vimEnabled()` / `setVimEnabled()`，存 `<dataDir>/settings.json` | — |
| `tests/test_vim_qt.cpp` | 用真实 `QKeyEvent` 验证适配层，14 个测试 | Qt Widgets（offscreen） |
| `tests/test_vim_window.cpp` | 窗口级回归：Esc 关闭规则、插入状态与原生输入同步，8 个测试 | Qt Widgets（offscreen） |

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
- **Esc 归当前焦点所有**：Vim 开启且文本编辑器有焦点时，Esc 由 Vim 处理（Insert 退出到
  Normal）；其他焦点或 Vim 关闭时，Esc 关闭窗口。它不是窗口级 `QShortcut`，避免抢先拦截。
- **窗口快捷键保留**：Ctrl+S 执行、Ctrl+Z/Y 仍是宿主撤销、Alt 与 F 键放行；
  唯一的 Ctrl 组合是 Ctrl+R（Vim 重做）。
- **撤销归核心所有**：`u` / Ctrl+R 走 vimcore 自己的快照，一次插入会话算一步，
  不影响控件自身的撤销栈。
- 切换模式或外部重载缓冲区时会 `resync()`，把核心的文字与光标重新对齐；原生 Insert
  输入则使用 `refreshFromDocument()` 更新同一个核心实例，不重置模式或撤销会话。

## v1 支持范围

Normal / Insert / Visual / Visual Line；`h j k l w b e 0 ^ $ gg G f F t T`；
`i a I A o O s`；`d c y` 配 motion 与叠用（`dd cc yy`）、`D C S x X p P J r`；
计数前缀（`3w`、`2dd`、`2dw` = `d2w`）；无名寄存器；`;` `,` 重复查找。

不做（有意）：ex 命令行、命名寄存器、宏、marks、`/` `?` 搜索、`g` 前缀（`gg` 除外）、
多缓冲区。新增按“小而带测试”的增量来加。
