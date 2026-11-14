# QQSpeedLauncher 启动诊断设计

## 目标

为“Portable 进程出现后数秒退出、界面从未显示”的问题建立可复验的启动证据链，能够区分：

1. Portable 外层未完成启动（UAC、TEMP 解包、杀软/策略拦截、文件损坏）；
2. Electron 主进程初始化失败；
3. BrowserWindow、renderer 或 GPU/utility 子进程失败；
4. Native worker / NativeBridge 加载失败。
5. Portable 外层与内层 Electron 提权级别不一致。

本次只增加诊断能力，不修改登录协议、GameApp 启动时序、服务器地址、Hook 内容或 VMP 产物。

## 方案比较

### 方案 A：只启用现有 `launcher.log`

- 优点：改动最少。
- 缺点：日志受 `QQSPEED_LAUNCHER_DEBUG=1` 控制，而且只有 Electron 和 NativeBridge 已运行后才可能产生；无法解释当前最关键的“外层秒退”。
- 结论：不采用。

### 方案 B：直接修改 electron-builder 的 Portable NSIS 模板

- 优点：能在单个 EXE 内记录解包前后阶段。
- 缺点：当前 electron-builder 26.15.3 的 portable target 固定读取其内置 `portable.nsi`，忽略项目级 `nsis.script/include`；需要修改 `node_modules` 或维护自建 Portable 打包器，容易被安装依赖覆盖，违背“构建不得自动覆盖/复制旧产物”的要求。
- 结论：不采用。

### 方案 C：应用内分层日志 + 外部一次性启动采集器（采用）

- Electron 包内增加最早期同步日志，记录主进程、窗口、renderer、GPU/utility 和退出原因；native worker 记录 DLL 加载边界。
- 项目提供一个只读的一键采集器，围绕现有 Portable 记录外层进程、哈希、签名、UAC/启动错误、TEMP 可写性和相关 Windows 崩溃/拦截事件。
- 若外层出现但应用内日志完全不存在，可直接归类到 Portable 解包/安全软件/系统策略层；若应用内日志存在，则继续按最后一个阶段定位。
- 不修改 electron-builder 依赖文件，不引入新的 C++ EXE，不复制或解密 Hook。
- Portable 外层使用 `requestExecutionLevel=user`，内层使用 `requestedExecutionLevel=asInvoker`；登录器直接按当前用户权限打开，不在 Electron/Chromium 多进程启动阶段触发 UAC。

## 组件设计

### 1. Electron 最早期日志

- 新增独立 bootstrap 入口，并由 `package.json.main` 指向它。
- bootstrap 在动态导入现有 `src/main/index.js` 前创建日志并注册：
  - `uncaughtException`
  - `unhandledRejection`
  - `exit`
- 使用同步追加写入，确保进程秒退前日志已经落盘。
- 日志路径：`%LOCALAPPDATA%\QQSpeedLauncher\logs\startup.log`。
- 每次启动产生 `run_id`，每行包含时间、PID、组件、阶段和有限字段。
- 日志达到 2 MiB 时轮换为 `startup.log.1`，仅保留当前及上一份。
- 日志写入失败不得阻断登录器启动。

### 2. Electron 生命周期阶段

`src/main/index.js` 记录以下边界：

- `MAIN_IMPORT_BEGIN / MAIN_IMPORT_OK / MAIN_IMPORT_FAILED`
- `SINGLE_INSTANCE_LOCK_ACQUIRED / REJECTED`
- `APP_READY_BEGIN / APP_READY_OK / APP_READY_FAILED`
- `WINDOW_CREATE_BEGIN / WINDOW_CREATED`
- `RENDERER_LOAD_BEGIN / RENDERER_LOAD_OK / RENDERER_LOAD_FAILED`
- `WINDOW_READY_TO_SHOW / WINDOW_SHOWN`
- `RENDERER_PROCESS_GONE / WINDOW_UNRESPONSIVE`
- `CHILD_PROCESS_GONE`（包含 GPU、utility 等类型和退出原因）
- `APP_BEFORE_QUIT / APP_EXIT`

只记录错误码、reason、exitCode 和阶段耗时；不记录 renderer console 内容。

### 3. Native 边界日志

`src/main/native-worker.js` 复用同一日志模块，记录：

- worker 创建、spawn、exit 和 error；
- runtime staging 开始/成功/失败；
- Koffi 加载 NativeBridge 开始/成功/失败；
- native session 启动和最终状态。

不记录账号、密码、登录票据、共享内存内容、服务器密钥或 Hook 内容。现有 C++ `launcher.log` 仍保持显式调试开关，不在公开构建中自动开启。

### 4. Portable 外层一键采集器

- 新增 PowerShell 采集脚本和 `.cmd` 入口；客户把它与 Portable EXE 放在同一目录，双击一次即可。
- 采集器不修改系统配置，不关闭杀软，不申请持久权限，不复制 Hook。
- 采集内容：
  - Portable 文件名、大小、SHA-256、Authenticode 状态；
  - Windows 版本、架构、管理员状态、TEMP 可写性和可用空间；
  - Portable 及内层 `QQSpeedLauncher.exe` 的出现、PID、退出码和持续时间；
  - 本次时间窗内与 QQSpeedLauncher 相关的 Application Error、Windows Error Reporting、Defender 和 Code Integrity 事件；
  - 本次生成的 `startup.log` 与 Electron 标准日志（若存在）。
- 输出到桌面 `QQSpeedLauncher-Diagnostics-<时间>.zip`，同时生成不含敏感数据的 `summary.txt`。
- 采集不到某一事件日志时写入 `UNAVAILABLE`，不得把“无权限读取”误报为“没有拦截”。

### 5. Portable 权限与 TEMP 行为

- `build.portable.requestExecutionLevel` 固定为 `user`，`build.win.requestedExecutionLevel` 固定为 `asInvoker`，避免登录器打开前的提权依赖，并与已验证可用的旧原生登录器权限模型保持一致。
- `build.portable.unpackDirName` 固定为 `false`，每次启动使用唯一 `$PLUGINSDIR`，避免同一产物的并发实例互相删除固定 TEMP 目录。
- 不修改 inner Electron、Hook 或 NativeBridge 的权限和业务逻辑。

## 隐私与安全

- 所有结构化字段采用白名单；错误文本执行用户目录、账号、密码、token、ticket、cookie、Authorization 等模式脱敏。
- 不记录命令行中的凭据，不抓包，不读取浏览器、系统凭据或无关用户目录。
- 不包含 DLL、Hook、内存 Dump 或游戏文件副本。
- 采集器仅查看启动时间窗内、且消息命中 QQSpeedLauncher/相关产物名的系统事件。

## 判定规则

| 证据 | 结论范围 |
|---|---|
| Portable 未启动，启动错误为 1223 | 用户取消 UAC |
| Portable 启动后退出，`startup.log` 完全不存在 | 外层解包、杀软、TEMP 或系统策略 |
| `MAIN_IMPORT_FAILED` | Electron 模块/ASAR/原生 Node 模块初始化 |
| `APP_READY_OK` 后无 `WINDOW_CREATED` | 主窗口创建阶段 |
| `RENDERER_LOAD_FAILED` | renderer 文件、协议或加载错误 |
| `RENDERER_PROCESS_GONE` / GPU `CHILD_PROCESS_GONE` | renderer/GPU 兼容或崩溃 |
| `WINDOW_SHOWN` 后 Native worker 失败 | Native runtime、Koffi 或 NativeBridge 边界 |
| Defender/Code Integrity 同时命中 | 安全软件或系统策略拦截，有直接事件证据 |

日志只能定位失败层；在拿到一台失败机器的证据前，不把任何候选原因宣称为最终根因。

## 验证

1. 先写失败测试验证日志模块、脱敏、轮换和必需阶段尚不存在。
2. 实现后运行现有 `npm run check` 和所有 smoke tests。
3. 构建 unpacked 与 Portable 产物，实际启动并确认 `WINDOW_SHOWN` 阶段存在。
4. 人工制造可控失败：错误 renderer 路径、重复实例、native worker DLL 不存在；验证最后阶段与预期一致，随后恢复原产物。
5. 运行一键采集器，验证 ZIP 中只有允许文件，且不含 password/token/ticket/cookie 等敏感值。
6. 重新计算并比对 `qqspeed_clean_hook_64.dll` SHA-256，必须与改动前完全一致。
7. 检查构建脚本未修改 `node_modules`、未从旧目录复制 Hook、未新增 C++ 启动器。
8. 提取最终 Portable 与内层 PE manifest，验证两者均不要求管理员权限。

## 回滚

- 恢复 `package.json.main` 到原入口并删除新增日志/采集文件即可；业务文件和 Hook 不需要回滚。
- 日志写入均为 best-effort，任何写盘异常不参与业务分支。
