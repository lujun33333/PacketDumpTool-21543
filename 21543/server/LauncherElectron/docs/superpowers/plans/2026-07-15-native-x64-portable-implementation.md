# Native x64 Portable Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用真正的 Windows x64 最小启动壳替换 electron-builder 的 x86 NSIS Portable 外层，同时保留单 EXE、当前 Electron 业务与固定 Hook 制品。

**Architecture:** electron-builder 继续只生成 `dist/win-unpacked` x64 应用负载；独立 Rust GUI stub 解析追加在自身 PE 后的 STORE 归档，校验后解包、转发参数并启动内部 Electron。Node 构建脚本负责稳定归档、构建期资源校验和带回滚的同卷 staged replacement，smoke 负责验证架构、manifest、归档一致性和业务边界。

**Tech Stack:** Electron 35.7.5、Node.js 20+ CJS、electron-builder 26.15.3（仅 unpacked）、Rust 1.90 / edition 2024、`x86_64-pc-windows-msvc`、SHA-256、Windows GUI PE。

## Global Constraints

- 外层、内部 Electron、NativeBridge 和 Hook 必须全部为 x64 PE32+。
- 不新增或复用旧 C++ 登录器；Rust 外层不得包含登录、GameApp、Hook、注入、共享内存、服务器或 IP 逻辑。
- 客户交付仍为一个 `QQSpeedLauncher-Portable-1.0.0-x64.exe`。
- 不修改 `node_modules`，不从旧目录复制 DLL，不修改 Hook/VMP 内容。
- `qqspeed_clean_hook_64.dll` 的大小和 SHA-256 必须与 `resources/native/runtime-artifacts.json` 一致；打包脚本不得另行硬编码版本哈希。
- 外层与内层均使用 `asInvoker`，不得要求管理员权限。
- 项目没有 Git 元数据，commit 步骤由可见 diff、原产物备份和哈希证据替代。

---

### Task 1: Portable x64 发布门禁（RED）

**Files:**
- Create: `src/scripts/smoke-portable-x64.cjs`
- Modify: `package.json`

**Interfaces:**
- Produces: `readPeInfo(filePath)`、`readSfxIndex(filePath)`、`verifyPortableX64()` 和 `npm run check:portable-x64`。

- [x] 写 smoke，断言正式外层 `Machine=0x8664`、PE32+、GUI、asInvoker，并要求存在自定义 SFX footer。
- [x] 让 smoke 解析 index，并与 `dist/win-unpacked` 的路径、大小和 SHA-256 完全一致。
- [x] 扫描未来 Rust stub 源码的禁止业务标识符。
- [x] 对当前 NSIS 产物运行 smoke，保留因 `actual=0x014c` 失败的 RED 证据。

### Task 2: 最小 Rust x64 启动壳（GREEN 单元）

**Files:**
- Create: `native/portable-x64-bootstrap/Cargo.toml`
- Create: `native/portable-x64-bootstrap/Cargo.lock`
- Create: `native/portable-x64-bootstrap/src/archive.rs`
- Create: `native/portable-x64-bootstrap/src/main.rs`

**Interfaces:**
- `archive::read_archive(&mut File) -> Result<Vec<Entry>, ArchiveError>`
- `archive::extract_archive(&mut File, &[Entry], &Path) -> Result<(), ArchiveError>`
- `Entry { relative_path: PathBuf, offset: u64, length: u64, sha256: [u8; 32] }`

- [x] 先写 Rust 单元测试覆盖合法 index、magic/version 错误、越界、重复路径、绝对路径、`..` 和 hash 不匹配。
- [x] 运行 `cargo test`，确认缺失实现导致失败。
- [x] 实现固定 footer/index 解析、路径验证和流式 SHA-256 解包，使单元测试通过。
- [x] 实现 GUI 入口、唯一 TEMP 目录、Portable 环境变量、参数转发、子进程等待、清理重试、错误框及脱敏日志。
- [x] 运行 `cargo fmt --check`、`cargo clippy --release --target x86_64-pc-windows-msvc -- -D warnings` 和 `cargo test`。

### Task 3: Node SFX 构建器与资源

**Files:**
- Create: `native/portable-x64-bootstrap/app.manifest`
- Create: `src/scripts/build-portable-x64.cjs`
- Modify: `package.json`

**Interfaces:**
- Consumes: `dist/win-unpacked`、Rust release stub、`assets/app.ico`、`resources/native/runtime-artifacts.json`。
- Produces: `dist/QQSpeedLauncher-Portable-1.0.0-x64.exe`。

- [x] 先扩展 Node smoke，要求 `package.json` 的 Portable 构建链不再调用 electron-builder `portable` target，并确认失败。
- [x] 构建器验证内部 x64 入口和固定 runtime artifacts，调用 Cargo 构建 release x64 stub。
- [x] 在 Rust 链接阶段嵌入图标、版本信息和 `asInvoker` manifest，并在追加 overlay 前校验。
- [x] 稳定排序并流式附加所有 unpacked 文件，写 index/footer 后使用同卷 staged rename；失败自动恢复上一正式产物。
- [x] 修改 `dist:portable` 为 `electron-builder --dir --x64` 后调用 x64 SFX 构建器；`dist:all` 同步使用该构建器，不再生成 x86 Portable。

### Task 4: 构建与结构验证

**Files:**
- Build: `dist/win-unpacked/**`
- Build: `dist/QQSpeedLauncher-Portable-1.0.0-x64.exe`

**Interfaces:**
- Produces: 可复验的 x64 单文件 Portable。

- [x] 记录现有 x86 Portable 和 Hook 的 SHA-256，保留原产物身份哈希。
- [x] 运行 `npm run dist:portable`。
- [x] 运行 `npm run check:portable-x64`，确认 PE、manifest、GUI subsystem、archive 文件集和所有 SHA-256 一致。
- [x] 验证最终外层没有 GameApp、Hook、IP、登录或注入业务逻辑，Rust 源码也无这些边界违规。
- [x] 确认构建未修改 `node_modules`，未从旧目录复制 DLL。

### Task 5: 实际运行与完整回归

**Files:**
- Verify only; no additional production files unless a test exposes根因。

**Interfaces:**
- Consumes: Task 4 final artifact。
- Produces: 本机中等完整性启动证据、GameApp 预加载证据和完整回归结果。

- [x] 在非管理员中等完整性上下文直接启动正式 Portable，确认真实 renderer/preload API 并记录启动耗时。
- [ ] 使用隔离 `--user-data-dir` 和真实 `F:\QQ飞车` 验证既有 GameApp 预加载成功。已复现 replacement GameApp 的 `OpenProcess error=5`；直接启动 inner Electron 同样失败，确认不是 x64 外层回归，留待 NativeBridge 专项修复。
- [x] 运行 `npm run check`、`npm run verify:runtime`、native boundary、renderer flow、credentials、lifecycle、endpoint、fuses 和 packaged smoke；旧 release smoke 强制交互式 `RunAs`，本轮以更相关的非管理员正式外层 runtime smoke 替代。
- [x] 重算 source、unpacked 和 SFX 内 Hook SHA-256，三者完全一致。
- [x] 检查 Authenticode 状态、残留 launcher/GameApp 进程和 TEMP 清理；客户电竞系统仍列为未验证环境。

### Task 6: 收口与正式命名

**Files:**
- Modify: `README.md`
- Update: `docs/superpowers/specs/2026-07-15-native-x64-portable-design.md`（仅当验证发现设计差异）

**Interfaces:**
- Produces: 唯一正式 x64 Portable、构建命令、哈希与回滚说明。

- [x] README 明确 `dist:portable` 现在生成真正 x64 外层，electron-builder 只负责 unpacked payload。
- [x] 旧正式路径只在新 candidate 完整验证后通过 staged rename 替换；不执行额外删除动作。
- [x] 列出 changed files、最终路径、大小、SHA-256、PE machine、manifest、签名状态、测试结果和未验证环境。
