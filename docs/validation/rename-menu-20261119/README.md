# 结果菜单与目录导航验证（2026-11-19）

## 结论

| 项目 | 结果 |
|---|---|
| 测试 | `ctest` 7/7：`vim_qt`、`vim_window`（32 条，含新增的 `GoingUpKeepsTheTextTab`）、`rename_core`（108 条，含跨目录计划用例）、`rename_e2e`（含 LS-S7-11）、`core_purity`、`action_payload`、`manifest_menu_switch` |
| 清单门禁 | `ctest manifest_menu_switch` 通过；`manifest-gate.txt` 里的 5 组临时负例全部判失败（漏 `if`、`default:false`、`if` 指向 `text` 项、错键名、`minWinShun 0.32.0`） |
| 包 | `ra9fael.rename-0.5.1.wsplugin`，3,628,175 字节，SHA-256 `a2e1ce58cf4262f7e382a480ac2fae846bf2245a8192273b251b2eebbb39106e` |
| 安装 | `ws plugin install`：0.5.1，`state=on`、`status=ready`，权限 `ui`/`files:read`/`files:write`，无清单错误（WinShun 0.38.0） |
| 宿主机制 | WinShun 自带 `tst_plugin` 的 `filesTriggerSetting`、`filesTriggerSettingChecked` 通过：设置开着菜单项在、关掉不在、再开回来在 |
| 关键词入口 | `tools/host-sim.ps1` 里关键词只返回一行：无词时“打开批量重命名…”（`data.openWindow`），在文件夹里搜索时“在此文件夹批量重命名…”（`data.openFolder`）；两行都能打开窗口，不是“没有选中文件” |
| 选择集 | `files` 动作带一个文件加一个文件夹：stderr 记下 `files trigger: count=2 chars=…` 和每个路径，窗口输入集就是这两项 |
| 导航保留 tab | `vim_window` 的 `GoingUpKeepsTheTextTab`：在子目录里切到文本 tab，`goUp()` 之后仍是文本模式，缓冲区换成父目录的直接子项（父目录的 `alpha.txt` 在，子目录的 `beta.txt` 不在） |
| 未做 | 真机右键菜单与关键词行的**人工点击**取证：两者都由宿主渲染，本机只验证到“清单被宿主接受、插件就绪、宿主该机制的单测通过、插件的协议往返正确”，没有在弹出的菜单和搜索框里逐项点击 |

## 证据

- `ctest.txt`：本机 7/7。
- `manifest-gate.txt`：清单门禁正例、5 组负例的输出与退出码（由 `negatives.ps1` 生成，可复跑），以及动作 payload 门禁。
- `host-files-if.txt`：宿主 `tst_plugin` 的两个 `files.if` 用例。
- `host-sim-keyword.txt`：关键词不带词的往返——一行 `batch.hint`、`data.openWindow`，回车打开空窗口。
- `host-sim-folder.txt`：在文件夹里搜索的往返——一行 `batch.here`、`data.openFolder` 指向该文件夹。
- `host-sim-files.txt`：右键入口的 `files` 动作（一个文件加一个文件夹）走同一个开窗路径，stderr 逐条列出路径。
- `host-plugin.txt`：`ws plugin list --json` 里 `ra9fael.rename` 的条目（0.5.1，无 `winshun:search`）。
- `package.txt`：本次包的路径、大小与 SHA-256。
- `window-test.txt`：`vim_window_tests` 全部 32 条的输出，含 `GoingUpKeepsTheTextTab`。
- `single-file.png`、`single-folder.png`、`one-folder.png`：`rename-ui.exe --files` 的窗口渲染图——单文件一行、单文件夹是文件夹自身（不是它的内容）、同目录两个文件两行且只显示名字。
- `span-folders.png`：跨目录同名两个文件——两行都“可改”，原始列显示 `same.txt · …\span\a` / `…\span\b`，悬停给完整目录。
- `blocked-selection.png`：`alpha.txt` 加一个不存在的路径：0 行、红字说明找不到的路径、底部显示原因、`重命名` 按钮不可用。

## 复现

```powershell
git submodule update --init --recursive
cmake --build build-qt --parallel
ctest --test-dir build-qt --output-on-failure          # 7/7
pwsh -File tools/check-manifest.ps1                    # clean
pwsh -File docs/validation/rename-menu-20261119/negatives.ps1   # five variants refused
pwsh -File tools/pack.ps1 -BuildDir build-qt -OutDir dist\rename-menu-validation
pwsh -File tools/host-sim.ps1 -Exe .\rename.exe -QueryText ''
pwsh -File tools/host-sim.ps1 -Exe .\rename.exe -Folder D:\some\dir -QueryText 'x'
pwsh -File tools/host-sim.ps1 -Exe .\rename.exe -Files <一个文件> <一个文件夹>
ws plugin install dist\rename-menu-validation\ra9fael.rename-0.5.1.wsplugin
```

本机 `PATH` 上有会抢 `Qt6Widgets.dll` 的 Qt 副本（MiKTeX），所以 `rename.exe`、`rename-ui.exe`、
`vim_window_tests.exe` 与 `tst_plugin.exe` 运行时把 Qt 6.12 的 `bin` 放在最前；包里的
`Qt6Widgets.dll` 与 exe 同级，宿主启动时先找到它，不受这条影响。