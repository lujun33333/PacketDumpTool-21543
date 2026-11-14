# QQSpeedLauncher Startup Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 定位 Portable 秒退发生在外层、Electron、renderer/GPU 还是 NativeBridge 边界，并消除固定 TEMP 并发冲突与外层/内层延迟提权不一致。

**Architecture:** 使用零依赖 JSONL 同步日志作为 Electron 与 utility process 的共同诊断通道；应用外提供只读 PowerShell 采集器补齐 electron-builder Portable 外层证据。通过 `portable.unpackDirName=false` 使用每次启动唯一的 NSIS 插件目录；Portable 与 Electron 主程序均按当前用户权限运行，不在打开登录器时提权；不修改 Hook、登录协议或 C++ 启动逻辑。

**Tech Stack:** Electron 35.7.5、Node.js ESM、electron-builder 26.15.3、PowerShell 5.1、NSIS portable target。

## Global Constraints

- 不修改登录协议、GameApp 启动时序、服务器地址、Hook 内容或 VMP 产物。
- 不修改 `node_modules`，不从旧目录复制 DLL，不新增 C++ EXE。
- 日志不得记录账号、密码、token、ticket、cookie、Authorization、共享内存内容或 Hook 内容。
- 日志写入必须 best-effort；失败不得阻断业务启动。
- 最终 Hook 的大小和 SHA-256 必须与 `resources/native/runtime-artifacts.json` 一致；构建只校验，不从旧目录回拷。
- 项目没有 Git 元数据，所有 commit 步骤以可见 diff 及验证记录替代。

---

### Task 1: 启动日志核心

**Files:**
- Create: `src/main/startup-log.js`
- Create: `src/scripts/smoke-startup-diagnostics.cjs`

**Interfaces:**
- Produces: `initializeStartupLog()`, `logStartupEvent(component, event, fields)`, `serializeStartupError(error)`, `redactStartupText(value)`, `getStartupLogPath()`。

- [ ] 先写 smoke 测试，要求日志生成 JSONL、敏感字段脱敏、用户目录脱敏、超过 2 MiB 时轮换。
- [ ] 运行 `node src/scripts/smoke-startup-diagnostics.cjs`，确认因模块不存在而失败。
- [ ] 实现同步追加、字段白名单、2 MiB 单备份轮换和全路径 best-effort。
- [ ] 重跑 smoke，确认通过且临时目录清理完成。

### Task 2: Electron 最早入口与生命周期

**Files:**
- Create: `src/main/bootstrap.js`
- Modify: `package.json`
- Modify: `src/main/index.js`
- Modify: `src/scripts/smoke-startup-diagnostics.cjs`

**Interfaces:**
- Consumes: Task 1 日志 API。
- Produces: `MAIN_IMPORT_*`、`APP_READY_*`、`WINDOW_*`、`RENDERER_*`、`CHILD_PROCESS_GONE` 和退出事件。

- [ ] 扩展 smoke，断言 `package.json.main` 指向 bootstrap 且必需阶段均有注册；运行并确认失败。
- [ ] bootstrap 在动态导入 `index.js` 前注册 fatal handler 和退出日志。
- [ ] 在 `index.js` 的单实例、ready、BrowserWindow、webContents、GPU/utility 和 quit 边界写阶段日志。
- [ ] 运行语法检查与 startup smoke，确认通过。

### Task 3: Native worker 边界

**Files:**
- Modify: `src/main/index.js`
- Modify: `src/main/native-worker.js`
- Modify: `src/scripts/smoke-startup-diagnostics.cjs`

**Interfaces:**
- Consumes: Task 1 日志 API和 Task 2 的 `run_id` 环境变量。
- Produces: `NATIVE_STAGE_*`、`NATIVE_WORKER_*`、`NATIVE_BINDINGS_*`、`NATIVE_SESSION_*`。

- [ ] 扩展 smoke，断言 worker/runtime/DLL load 的成功与失败阶段；运行并确认失败。
- [ ] 主进程记录 runtime staging、fork、spawn、error、exit。
- [ ] worker 只记录验证、Koffi load 和 session 结果，不记录传入 payload。
- [ ] 重跑 startup smoke 与 native boundary smoke。

### Task 4: Portable 外层采集器

**Files:**
- Create: `diagnostics/QQSpeedLauncher-Startup-Diagnose.ps1`
- Create: `diagnostics/运行启动诊断.cmd`
- Modify: `src/scripts/smoke-startup-diagnostics.cjs`

**Interfaces:**
- Produces: 桌面 `QQSpeedLauncher-Diagnostics-<timestamp>.zip`，内含 `summary.txt`、`processes.jsonl`、相关事件和本次应用日志。

- [ ] 扩展 smoke，检查脚本可解析、仅采集白名单事件且不含关闭杀软/复制 DLL/读取凭据操作；运行并确认失败。
- [ ] 实现文件身份、TEMP 写入、进程观察、相关 Application/Defender/CodeIntegrity 事件采集与自动分类。
- [ ] `.cmd` 使用 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File` 调用同目录脚本。
- [ ] 运行 PowerShell parser 和 `-NoLaunch` 自测，确认生成 ZIP 且不含 DLL/DMP/凭据。

### Task 5: 消除固定 TEMP 并发冲突和延迟提权

**Files:**
- Modify: `package.json`
- Modify: `src/scripts/smoke-startup-diagnostics.cjs`

**Interfaces:**
- Produces: `build.portable.unpackDirName=false`、`requestExecutionLevel=user` 与 `requestedExecutionLevel=asInvoker`，使每次运行使用唯一 `$PLUGINSDIR` 且双击后不显示 UAC。

- [ ] 先令 smoke 断言 `unpackDirName=false`、`requestExecutionLevel=user` 和 `requestedExecutionLevel=asInvoker`，运行确认失败。
- [ ] 只修改这两个 Portable 配置，不更改压缩、inner 权限或 artifactName。
- [ ] 重跑 startup smoke 和完整 `npm run check`。

### Task 6: 构建与安装态验证

**Files:**
- Build output: `dist/QQSpeedLauncher-Portable-1.0.0-x64.exe`

**Interfaces:**
- Consumes: Tasks 1-5。
- Produces: 可发送给失败客户的新版 Portable 与诊断采集器。

- [ ] 记录改动前 Hook、NativeBridge 和 Portable 哈希。
- [ ] 运行 `npm run check`、native/runtime/renderer/lifecycle/credential/fuse/packaged/release smoke。
- [ ] 运行 `npm run dist:portable`，确认构建未复制旧 DLL且未修改 `node_modules` 模板。
- [ ] 实际启动 Portable，确认 `WINDOW_SHOWN` 出现在 `%LOCALAPPDATA%\QQSpeedLauncher\logs\startup.log`。
- [ ] 提取 Portable 与内层 PE manifest，确认两者均为当前用户权限运行。
- [ ] 运行采集器短时测试并检查 ZIP 清单及脱敏扫描。
- [ ] 重新计算 Hook 哈希，必须与全局约束一致；报告新 Portable 路径、大小、SHA-256、签名状态和仍未验证的失败客户环境。
