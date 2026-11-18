# 结果菜单与关键词搜索验证（2026-11-19）

## 结论

| 项目 | 结果 |
|---|---|
| 测试 | `ctest` 7/7：`vim_qt`、`vim_window`、`rename_core`（112 条，含新增的跨目录计划用例与搜索行用例）、`rename_e2e`（含 LS-S7-11）、`core_purity`、`action_payload`、`manifest_menu_switch` |
| 清单门禁 | `ctest manifest_menu_switch` 通过；`manifest-gate.txt` 里的 6 组临时负例全部判失败（漏 `if`、`default:false`、`if` 指向 `text` 项、错键名、`minWinShun 0.32.0`、删掉 `winshun:search`） |
| 包 | `ra9fael.rename-0.5.0.wsplugin`，3,642,655 字节，SHA-256 `44198d1a5d93edc19398f3a67491f843532c4ca14ad9a463be11dce9881d1c1c` |
| 安装 | `ws plugin install`：0.5.0，`state=on`、`status=ready`，权限 `ui`/`files:read`/`files:write`/`winshun:search`，无清单错误（WinShun 0.38.0） |
| 宿主机制 | WinShun 自带 `tst_plugin` 的 `filesTriggerSetting`、`filesTriggerSettingChecked` 通过：设置开着菜单项在、关掉不在、再开回来在 |
| 关键词搜索 | `tools/host-sim.ps1` 里插件真的发出 `winshun/tool` 的 `search_files`（`kind=files`、`size=false`、`limit=200`），在文件夹里搜索时还带上 `folders:[该文件夹]`；拿回两条匹配后返回“批量重命名匹配的 2 项…”加两个文件行；回车首行把两条路径原样交给窗口（stderr 逐条记录）。没有匹配时返回“没有匹配「zzz」的文件” |
| 选择集 | `files` 动作与搜索行都走同一个 `ShowSelection`：stderr 记下 `window: count=N` 和每个路径；窗口渲染图确认单文件 1 行、单文件夹是它自己、同目录两文件 2 行、跨目录同名两文件 2 行且每行带自己的目录、含读不到的路径时 0 行且按钮禁用 |
| 未做 | 真机右键菜单与关键词列表的人工点击取证：两者都由宿主渲染，本机只验证到“清单被宿主接受、插件就绪、宿主该机制的单测通过、插件的协议往返正确”，没有在弹出的菜单和搜索框里逐项点击 |

## 证据

- `ctest.txt`：本机 7/7。
- `manifest-gate.txt`：清单门禁正例，以及 6 组负例的输出与退出码（由 `negatives.ps1` 生成，可复跑）。
- `host-files-if.txt`：宿主 `tst_plugin` 的两个 `files.if` 用例。
- `host-sim-search.txt`：关键词搜索的完整往返——`search_files` 请求、宿主形态的回答、三行结果、首行动作、stderr 里的两条路径。
- `host-sim-search-empty.txt`：没有匹配时的那一行。
- `host-sim-files.txt`：右键入口的 `files` 动作（一个文件加一个文件夹）走同一个开窗路径。
- `host-plugin.txt`：`ws plugin list --json` 里 `ra9fael.rename` 的条目。
- `package.txt`：本次包的路径、大小与 SHA-256。
- `single-file.png`、`single-folder.png`、`one-folder.png`：`rename-ui.exe --files` 的窗口渲染图——单文件一行、单文件夹是文件夹自身（不是它的内容）、同目录两个文件两行且只显示名字。
- `span-folders.png`：跨目录同名两个文件——两行都“可改”，原始列显示 `same.txt · …\span\a` / `…\span\b`，悬停给完整目录。
- `blocked-selection.png`：`alpha.txt` 加一个不存在的路径：0 行、红字说明找不到的路径、底部显示原因、`重命名` 按钮不可用。

## 复现

```powershell
git submodule update --init --recursive
cmake --build build-qt --parallel
ctest --test-dir build-qt --output-on-failure          # 7/7
pwsh -File tools/check-manifest.ps1                    # clean
pwsh -File docs/validation/rename-menu-20261119/negatives.ps1   # six variants refused
pwsh -File tools/pack.ps1 -BuildDir build-qt -OutDir dist\rename-menu-validation
Expand-Archive dist\rename-menu-validation\ra9fael.rename-0.5.0.wsplugin dist\rename-menu-validation\plugin
pwsh -File tools/host-sim.ps1 -Exe .\dist\rename-menu-validation\plugin\rename.exe `
    -QueryText txt -Hits D:\a\same.txt,D:\b\same.txt
pwsh -File tools/host-sim.ps1 -Exe .\dist\rename-menu-validation\plugin\rename.exe -Files <一个文件> <一个文件夹>
ws plugin install dist\rename-menu-validation\ra9fael.rename-0.5.0.wsplugin
```

本机 `PATH` 上有会抢 `Qt6Widgets.dll` 的 Qt 副本（MiKTeX），所以 `rename.exe`、`rename-ui.exe`
与 `tst_plugin.exe` 运行时把 Qt 6.12 的 `bin` 放在最前；包里的 `Qt6Widgets.dll` 与 exe 同级，
宿主启动时先找到它，不受这条影响。
