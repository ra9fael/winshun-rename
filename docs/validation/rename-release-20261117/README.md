# 发布记录 0.4.4（2026-11-17）

## 结论

| 项目 | 结果 |
|---|---|
| tag | `v0.4.4`（附注 tag）→ commit `5dbf8cb` |
| 工作流 | `.github/workflows/release.yml`，run [44045730029](https://github.com/ra9fael/winshun-rename/actions/runs/44045730029)，成功 |
| 构建 | windows-2022，VS 2022 x64 Release，Qt 6.12.0 `win64_msvc2022_64` |
| 测试 | CI `ctest` 6/6（`vim_qt`、`vim_window`、`rename_core`、`rename_e2e`、`core_purity`、`action_payload`） |
| 产物 | `ra9fael.rename-0.4.4.wsplugin`，3,613,135 字节，SHA-256 `6e91cee1ca65d227691d173b18e051c9bfee454351021c54d7518f8341665712` |
| 包内文件 | `plugin.json`（2,021）、`rename.exe`（1,333,760）、`Qt6Widgets.dll`（6,520,120） |
| 附件复核 | 重新下载发布的附件，算出的 SHA-256 与同页 `.sha256` 一致 |

Release 上的两个附件是这次运行自己产出的字节：同一个作业里构建、跑测试、用 `tools/pack.ps1`
打包，再由 `publish` 附上与包配对的 `.sha256`。

## 本机

| 检查 | 结果 |
|---|---|
| `ctest`（build-qt，Ninja Release） | 6/6，17.0 s |
| `vim_qt` / `vim_window` | 37 / 31 条通过 |
| `rename_e2e` | 48 passed |
| `vimcore_tests` / `vimcore_conformance` | 57 / 74 条通过（`vendor/vimcore` 独立构建） |
| 安装 | `ws plugin install`：`ra9fael.rename 0.4.4`，`enabled=True`，`status=ready` |

0.4.4 的行为差异（operator 配查找位移、选区 `r`、`:q`/`:q!`、非插入模式的输入法隔离）
在离屏测试与渲染图里逐条覆盖；真机输入法组合与真实鼠标点击仍计入
[已知问题](../../../README.md#已知问题)。

## 复现

```powershell
git submodule update --init --recursive
cmake -S . -B build-qt -G Ninja -DCMAKE_BUILD_TYPE=Release -DRENAME_BUILD_QT=ON `
  -DCMAKE_PREFIX_PATH=D:\tools\Qt\6.12.0\msvc2022_64
cmake --build build-qt --parallel
ctest --test-dir build-qt --output-on-failure
```

CI 与本地唯一不同的地方是生成器：CI 用 `-A x64`（VS 多配置），本地是 Ninja；
`CMakeLists.txt` 的 CRT 选择用 `$<IF:...>`，两种生成器下都只展开成一个值。
