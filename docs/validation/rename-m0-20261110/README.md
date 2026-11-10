# ra9fael.rename M0 验证与人工验收

[English](README.en.md)

日期：2026-11-10。状态：M0 探针与骨架阶段，**不是可发布插件**。精选机器可读证据保存在工作台主仓库 `WinShun/docs/validation/rename-m0-20261110/`。

## 1. 问题是什么

`ra9fael.rename` 的设计（`plugin-design.md`）和开发计划（`DEVELOPMENT-PLAN.md`）建立在六条**未验证假设**之上。任何一条不成立，架构都要改，越晚发现代价越大。M0 的目的就是在写产品代码之前把这六条测掉，并把可编译、可测试的骨架立起来。

环境：Windows x64、MSVC 19.51.36260.0（VS 18 Community）、CMake 3.29.2、Ninja、PowerShell 7。

## 2. 为什么

六条假设都是"设计依赖它、但没有任何实测支持"的事实：

| 假设 | 若不成立的后果 |
|---|---|
| P1 宿主 `files` 触发能送到插件 | 入口方案失效，产品形态要重做 |
| P2 共享 Qt 在本插件可用 | 必须自带 Qt 运行时，包体显著增大 |
| P3 `IFileOperation` 能在插件进程改名 | 执行模型（RNF-14/15）整体要改 |
| P4 提权提示的实际呈现 | 受保护目录的改名路径无法确定 |
| P5 `files` 数组 32000 字符上限 | 大批量选择的处理策略要改 |
| P6 WIC 元数据在插件内可读 | 元数据模板（RN-FR-16）要降级 |

先测再写，是为了避免"写完发现地基不对"。

## 3. 改了什么

新增独立插件仓库内容，**未修改主程序代码**：

- `core/`：纯规则核心（`rename_core.h` + 7 个实现文件）。硬约束：**不含任何 Windows 或 Qt 头文件**（开发计划 §4）。
- `tests/`：工作台约定的零依赖 `test_harness.h` 与 32 条 L1 用例。
- `probe/probe_main.cpp`：M0 探针入口，服务 SDK 协议并把 `files` 触发载荷记到 stderr（P1）。
- `package/plugin.json`：探针版清单（v0.0.1，`files` 触发 + `ui`/`files:read`/`files:write`）。
- `tools/check_core_purity.ps1`：`core/` 纯净性门禁，注册进 ctest。
- `CMakeLists.txt`：C++20、CMake 3.21+、x64 强制、静态 CRT（与 `findmouse` 一致）。

## 4. 实测

| 层 / 设计范围 | 方法 | 结果 |
|---|---|---|
| 构建 | `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release` + `cmake --build` | 通过；生成 `rename.exe`（探针）与 `rename_tests.exe`；无 error，无 warning（`/W4 /permissive-`）|
| L1 纯核心 | `ctest --test-dir build-msvc` → `rename_core` | **32/32 通过**。覆盖 scope 拆分（`archive.tar.gz`/`.gitignore`/`README`/尾随点）、NFC 等价与 `ß`≠`ss`、trim 边界（全空白、NBSP、非法字符）、四档大小写变换（含 Title Case 例外词与撇号缩写）、计数补零与超大写保护、计划状态聚合 |
| 架构门禁 | ctest `core_purity` | 通过；并做了**负例验证**：临时插入 `#include <windows.h>` 后门禁返回 1 并报出文件名与行号 |
| 清单 | JSON 解析 + 与 `plugin.schema.json` 对照 | 合法；`id`/`version`/`triggers.files`/`permissions` 均符合 schema 枚举 |

证据文件：

- `ctest.txt` — 两项测试的完整输出（含 32 条用例逐条 OK）
- `manifest-check.txt` — 清单字段核对

### 4.1 测试抓到的三个真实 bug

这三条都是实现错误，被 L1 用例当场拦下，不是设计问题：

1. **`FormatCounter` 全部返回空字符串**。格式串写成 `"%%ll"`，缺转换字符 `d`，`swprintf` 直接失败。修正为 `"%lld"` / `"%0<width>lld"`。
2. **Title Case 把 `don't` 切成两个词**，输出 `Don'T`。修正为：只在词首或分隔符之后把撇号当边界。
3. **`Capitalize` 同样按字符判分隔符**，与 (2) 同源。一并修正。

### 4.2 六条假设的当前状态

| 假设 | 状态 | 依据 |
|---|---|---|
| P1 `files` 触发 | **未测**（代码就位） | 探针能编译、能服务协议；还需在真实宿主里从结果菜单触发 |
| P2 共享 Qt | **未测** | 属 M3 前置，本阶段未触及 |
| P3 `IFileOperation` | **未测** | 属 M2 前置 |
| P4 提权提示 | **未测** | 同上 |
| P5 数组上限 | **未测**（探针已记录字符数） | 探针日志已输出 `files=N chars=M`，待真实选择验证 |
| P6 WIC 元数据 | **未测** | 属 M5 最后一项 |

**诚实结论：M0 只完成了"骨架 + 纯核心 + 门禁"，六条假设一条都还没验证。** 计划里写的是"P1–P3 全过才能进 M1/M2"，所以下一步是装探针到真实宿主跑 P1。

## 5. 没测的

- **宿主往返完全没有做**：探针未安装、未启动、未触发。`files` 触发是否真的送达、`files` 数组上限的真实行为、stderr 日志能否落盘，全部未验证。
- **Qt 一行都没写**：M3 的共享 Qt 契约、`appearance` 消息、QML 窗口均未接触。
- **`IFileOperation` 一行都没写**：深度倒序、身份复核、提权提示、SMB 行为全部未验证。`win/` 与 `ui/` 目录目前是空的。
- **正则路径未实现**：`RuleEngine` 的 literal 分支可用，regex 分支返回 `RuleError::Unsupported`。这是故意留的——半成品引擎不能被误认为能改名。
- **模板只实现了 `$$` 转义与计数占位**：日期模式表、负向前瞻、`${start=...,increment=...}` 参数解析都是 TODO。
- **oil 模式只有解析器和差异层**：没有缓冲区、没有 UI、没有 id 表同步。`OilDiffer` 的重复名检测未补 `UnknownId` 的用例覆盖。
- **所有"支持 Windows/SMB 实测"的结论都还是空的**：L3/L4 一条都没跑。
- 未验证 x86 构建是否真的被拦住（只加了 `CMAKE_SIZEOF_VOID_P` 检查，未实际尝试配置 32 位）。
