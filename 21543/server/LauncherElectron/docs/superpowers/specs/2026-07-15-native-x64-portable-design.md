# QQSpeedLauncher 原生 x64 Portable 外层设计

## 目标

将 `QQSpeedLauncher-Portable-1.0.0-x64.exe` 的外层从 electron-builder 生成的 x86 NSIS Portable（`Machine=0x014c`）替换为真正的 Windows x64 PE32+（`Machine=0x8664`），使外层与内部 Electron、NativeBridge、Hook 全部保持 x64，不再依赖 WOW64。

同时保持以下既有约束：

- 客户仍然只收到并双击一个 EXE；
- 不恢复 `LauncherKernel.exe`、`QQSpeedLauncher_x64.exe` 或旧 C++ 登录器逻辑；
- 新外层只负责校验、解包、启动内部 `QQSpeedLauncher.exe`、等待与清理；
- 不修改登录、GameApp 启动时序、LockedEndpoint、NativeBridge、Hook 或 VMP 内容；
- 构建前只校验固定运行时制品，不从旧目录复制 DLL；
- 外层与内层均以当前用户权限运行，不请求管理员权限。

## 已确认根因

- `electron-builder --win portable --x64` 只选择 x64 应用负载；其 Portable 启动器仍由 NSIS x86 模板生成。
- 当前外层 `dist/QQSpeedLauncher-Portable-1.0.0-x64.exe` 为 `Machine=0x014c`，内部 `dist/win-unpacked/QQSpeedLauncher.exe` 为 `Machine=0x8664`。
- 标准 Windows x64 可通过 WOW64 运行该外层，但精简电竞系统若移除或破坏 WOW64，外层会在 Electron 日志创建前秒退。
- 因此，仅重复传入 `--x64` 或修改 Portable 文件名不能修复；必须替换外层实现。

## 方案比较

### 方案 A：继续使用 NSIS Portable

- 优点：无需维护新代码。
- 缺点：外层仍是 x86，无法满足无 WOW64 的 x64 系统。
- 结论：不采用。

### 方案 B：改为安装包/MSIX

- 优点：可直接安装 x64 应用目录，减少每次解包时间。
- 缺点：不再是用户要求的单个免安装 EXE；还会引入安装、升级和卸载状态。
- 结论：不作为本次 Portable 替代方案。

### 方案 C：最小 Rust x64 自解压外层（采用）

- Rust 只生成打包启动器，不承载登录、游戏、注入、共享内存或服务器逻辑。
- 构建脚本把 `dist/win-unpacked` 原样以 STORE 方式附加到 x64 PE overlay，并写入带 SHA-256 的只读索引。
- 启动器流式校验并解包到每次启动唯一的临时目录，复制命令行，设置与 electron-builder Portable 兼容的三个环境变量，然后启动内部 x64 Electron。
- Electron 退出后清理临时目录；清理失败只记录诊断，不改变应用退出码。
- 优点：外层是真正 x64、单文件、无需旧 C++ 启动器、无需修改 `node_modules`。
- 代价：项目新增一个很小的 Rust 构建组件；Portable 仍必须把 Electron 文件落到临时目录后才能启动。

## 文件与职责

### `native/portable-x64-bootstrap`

- 独立 Rust 2024 binary crate，目标固定为 `x86_64-pc-windows-msvc`。
- Windows GUI subsystem，无控制台窗口。
- 只认识归档格式和内部入口名 `QQSpeedLauncher.exe`；源码不得出现 GameApp、Hook、登录、服务器 IP 或注入逻辑。
- 解析归档时限制文件数量、路径长度、索引大小和文件范围；拒绝绝对路径、盘符、`..`、重复路径及越界条目。
- 解包时逐文件计算 SHA-256；任何不匹配均不启动内部程序，并显示简短错误框及写入外层诊断日志。

### `src/scripts/build-portable-x64.cjs`

- 输入仅为当前 `dist/win-unpacked`。
- 先确认内部入口为 x64 PE32+，并调用现有只读运行时制品校验。
- 调用 Cargo release 构建 x64 stub；图标、版本信息和 `asInvoker` manifest 在 Rust 链接阶段通过 Windows SDK 资源编译器嵌入，并在附加 payload 前校验。
- 对文件按规范化相对路径稳定排序，流式附加内容，生成每文件大小与 SHA-256 索引；candidate 完整验证后使用同卷 staged rename 替换正式文件，失败自动恢复上一产物。
- 不读取旧 Launcher 目录，不复制或转换任何 DLL。

### `src/scripts/smoke-portable-x64.cjs`

- 验证外层和内层均为 `Machine=0x8664` / PE32+。
- 验证外层为 GUI subsystem 且 manifest 为 `asInvoker`。
- 解析 overlay，逐项比对 `dist/win-unpacked` 的相对路径、大小和 SHA-256。
- 验证归档中的 Hook 与 `resources/native` 固定制品哈希一致。
- 扫描 Rust 源码，阻止旧启动器、GameApp、Hook、服务器或注入逻辑进入外层。

## 归档格式

- Payload：文件内容原样连续存放，不压缩，避免 290 MiB 包在启动时额外解压 CPU 开销。
- Index：每项包含 UTF-8 相对路径、绝对 overlay 偏移、长度和 SHA-256。
- Footer：固定 magic、格式版本、条目数、index offset、index size 和 index SHA-256。
- 所有整数使用 little-endian；运行时先验证 footer/index，再创建临时目录。
- 该哈希用于完整性和损坏检测，不替代 Authenticode；正式公开发布仍应在完整 SFX 生成后进行代码签名。

## 运行流程

1. 打开自身并读取固定 footer。
2. 校验归档索引哈希、边界、路径和条目唯一性。
3. 在 `%TEMP%` 下以 PID、时间和排他创建组合生成唯一目录。
4. 流式解包并校验每个文件 SHA-256。
5. 设置 `PORTABLE_EXECUTABLE_DIR`、`PORTABLE_EXECUTABLE_FILE`、`PORTABLE_EXECUTABLE_APP_FILENAME`。
6. 原样转发用户命令行，启动临时目录中的 `QQSpeedLauncher.exe` 并等待。
7. Electron 退出后重试清理临时目录，返回内部程序退出码。

## 错误与诊断

- 外层错误写入 `%LOCALAPPDATA%\QQSpeedLauncher\logs\portable-x64.log`，只记录阶段、Windows error、耗时和归档错误，不记录账号、密码、token、ticket、共享内存或 Hook 内容。
- 归档损坏、TEMP 不可写、内部入口缺失或子进程创建失败时显示中文错误框。
- 外层日志失败为 best-effort，不得导致二次崩溃。

## 验证标准

1. 先让新 smoke 对现有 NSIS x86 外层失败，错误明确显示 `expected=0x8664 actual=0x014c`。
2. `cargo fmt --check`、`cargo clippy --release --target x86_64-pc-windows-msvc -- -D warnings`、`cargo test` 全部通过。
3. `npm run check` 及现有 native/runtime/renderer/credential/lifecycle/endpoint/fuse/package/release smoke 通过。
4. 正式 Portable 和内部 Electron 均为 x64、GUI、`asInvoker`。
5. 实际以非管理员中等完整性启动正式 Portable，窗口出现；用真实 `F:\QQ飞车` 验证既有 GameApp 预加载仍可运行。
6. 构建前后 `qqspeed_clean_hook_64.dll` 的大小和 SHA-256 必须与 `resources/native/runtime-artifacts.json` 一致；禁止在打包脚本中维护第二份硬编码哈希。
7. 最终报告签名状态；当前无证书时明确标记 `NotSigned`，不把 x64 兼容修复误报为安全软件信誉修复。

## 回滚

- 保留当前 NSIS Portable 到新 x64 外层完成全部验证；不先覆盖唯一可运行产物。
- 回滚时只恢复 `package.json` 的 Portable 命令并删除新增 Rust crate、构建脚本和 smoke；Electron、NativeBridge、Hook 不需要回滚。
- 项目没有 Git 元数据，使用文件级备份、可见 diff 和哈希记录代替 commit。
