# 架构说明

## 设计原则

当前实现不是把官服页面做成一张静态截图，也不是把旧 C++ 登录器套进 Electron。边界按官服可观察职责重新划分：

1. Electron main 管窗口、可信 IPC、账号策略、动态内容和会话生命周期；
2. 独立 `utilityProcess` 管私服协议、认证状态机和 GameApp 调度语义；
3. C++ DLL 只实现无法合理移出 Windows 原生层的进程、Hook、共享内存和握手能力；
4. `qqspeed_clean_hook_64.dll` 保留为哈希锁定的客户端兼容制品，并作为 RCDATA 嵌入 Bridge，而不是安装态裸 DLL。

## 分层与职责

```text
src/main/index.js
  Electron main：窗口、可信 IPC、预加载会话生命周期、登录/开始命令

src/main/native-core.js + credential-policy.js
  游戏目录、运行时制品、账号元数据与旧存储迁移

src/main/launcher-content-service.js
  官服动态配置获取、校验、缓存和离线回退

src/main/native-worker.js + session-state-machine.js + credential-vault.js
  独立 utilityProcess、私服认证、JS 状态机、Windows DPAPI 密码保险库、Koffi 原生调用调度

src/main/login/qq-tea.js
src/main/login/private-protocol.js
src/main/login/private-login-client.js
  QQ TEA、CSHEAD/MSGHEAD、TCP framing、超时/取消、登录结果解析

src/preload/preload.cjs
  renderer 白名单 API

src/renderer/content-view.js + media-controller.js
  数据驱动页面、图片/视频回退、独立视频/音乐控制

src/renderer/official-launcher-state.js + app.js
  renderer 认证/内容/游戏正交状态机；只接受 main 权威 session 事件切换登录页与首页

native/NativeBridge/
  DLL-only：GameApp Win32 生命周期、Hook 注入、共享内存/事件和必要握手

resources/native/
  源码侧固定 Hook、NativeBridge 构建产物/清单，以及发布所用的认证 qqspeed-runtime.bundle
```

不存在 `LauncherKernel.exe`、`QQSpeedLauncher_x64.exe` 或其他 C++ EXE target。

## 运行时状态机

```text
idle
  -- begin --> preloading
  -- native-preloaded --> preloaded

preloaded
  -- login --> authenticating
  -- auth-rejected --> preloaded

authenticating
  -- auth-succeeded --> authenticated

authenticated
  -- start --> starting

starting
  -- native-ready --> ready

任意非终态
  -- cancel/native-failed --> cancelled/failed
```

状态机权威在 `src/main/session-state-machine.js`，执行者在独立 `src/main/native-worker.js`。C++ 的数值状态是底层进程/共享内存事实来源，由 utility process 映射为对 UI 稳定的阶段。

上面是 **utility/GameApp 状态机**。renderer 另以 `official-launcher-state.js` 建模 `auth × game × content` 三个正交区域：

- 启动时固定为 `auth.booting + content.bootstrap/loading`；
- 登录页只加载 bootstrap `get_all_conf`；
- renderer 调用登录 IPC 后仍停留在登录视图，不能根据返回值直接切页；
- main 完成私服认证、原生 `ApplyAuth`、凭据提交和 session 提交后发布 `launcher:user-login`；只有该事件（或启动时 main 的已提交 session 快照）触发 `AUTH_SUCCEEDED/SESSION_RESTORED`，挂载首页并加载 home scope；
- `launcher:user-logout` 触发首页卸载并回到 bootstrap/login；
- native launch snapshot 只更新 game 区域，不能伪造认证态。

一致性门禁：main/worker 都在首个异步边界前取得独占 login owner；未认证的 `home` IPC 请求被降级到 bootstrap；登录/退出都会失效旧 home Promise；切换账号只在 native cancel 成功后清 session，cancel 失败保留原主页；GameApp 退出或 native 终态只为确有 remembered credential 的账号保留 launcher session，否则发一次 `UserLogout` 回登录页。

### 打开登录器：预加载

1. main 校验游戏目录和只读原生制品；
2. main 创建 utility process；
3. utility process 通过 Koffi 调用 `QQSpeedNativeBridge_BeginSession(gameRoot, server, hookHost, hookPort)`；
4. C++ 创建 `KartMap_Shared_<launcher pid>`、`RunSpeedGameEvent_<launcher pid>` 与 Hook 配置映射，启动 GameApp 并注入 Hook；
5. `RunSpeedGameEvent` 保持未触发，JS 状态进入 `preloaded`。

### 提交登录：JS 私服协议

1. utility process 从一次性 IPC 取得账号、临时密码和 remember/auto 选项；密码为空且账号已记住时，只在 utility process 内从 DPAPI 保险库解密；
2. `private-login-client.js` 连接锁定端点，处理超时、取消、半包和粘包；
3. `private-protocol.js` 负责 CSHEAD/MSGHEAD framing，`qq-tea.js` 负责 QQ TEA；
4. 锁定端点的 `passwordMode: 'md5'` 先生成与历史 Launcher/Server `MD5.h` 逐位一致的兼容摘要，再把该 wire password 作为 `SignKey`；它不是 RFC MD5，必须保留首轮零常量、错误的 word-vector padding、MSVC signed-char 扩展和 `A/B/C/D` 状态字拼接语义；成功响应解析为 `Uin + 16-byte GameKey`；
5. utility process 将 GameKey 转为 32 位 hex，调用 `QQSpeedNativeBridge_ApplyAuth(uin, gameKeyHex, signKey)`；
6. C++ 只校验并写入共享内存，不建立 socket、不执行登录协议；
7. utility process 观察原生 `authenticated` 后才写入/删除 DPAPI 密码保险库，并发送不含密码的 `credential-state`；
8. main 只提交账号元数据，随后 JS 状态进入 `authenticated`，GameApp 仍等待开始事件。

认证拒绝属于可恢复错误，状态返回 `preloaded`，可以重新输入。认证材料使用后会做尽力清零；JS 字符串受运行时限制，不能承诺物理内存级确定擦除。

### 点击开始：放行同一 GameApp

1. 只有 `authenticated` 状态接受开始命令；
2. utility process 调用 `QQSpeedNativeBridge_StartGame()`；
3. C++ 触发 `RunSpeedGameEvent` 并继续 Win32 握手/进程监控；
4. utility process 观察原生 ready 状态，将 JS 状态从 `starting` 切到 `ready`。

## DLL 最小 ABI

```text
BeginSession(gameRoot, server, hookHost, hookPort)
ApplyAuth(uin, gameKeyHex, signKey)
StartGame()
Cancel()
GetState()
GetProgress()
IsBusy()
GetGameProcessId()
GetLastError()
```

源码导出名统一带 `QQSpeedNativeBridge_` 前缀。旧 `RunSession` 与 `SubmitCredentials` 已移除；原生项目只编译：

```text
NativeBridge.cpp
NativeBridgeIpc.cpp
NativeBridgeLog.cpp
NativeBridgeProcess.cpp
```

原生工程不再包含 `PrivateLogin.cpp`、QQ TEA、MD5、锁定端点头文件或 Winsock 登录调用。

## 共享内存与 Hook

- `KartMap_Shared_<launcher pid>` 使用 `#pragma pack(1)` 的 `SharedData`，固定大小 `0xE9C`。
- 关键认证字段为 `Uin`、16 字节 `Key`、`SignKeyLen` 和最多 128 字节的 `SignKey`；当前 `ApplyAuth` 接受 1–64 字节 SignKey。
- `RunSpeedGameEvent_<launcher pid>` 是显式开始门；认证成功不会自动触发。
- `SpeedHookCfg_<GameApp pid>` 向保留 Hook 提供受控主机和端口，Hook 负责客户端网络重定向兼容。
- Hook 不负责 JS 私服登录，也不把私服凭据伪装成官服 MSDK `rawInfoJson`。

发布 Hook `resources/native/qqspeed_clean_hook_64.dll` 的身份由构建脚本固定 SHA-256；`build:native` 把它作为唯一 0x0409 RCDATA 编入 Bridge，并验证 PE 资源字节等于输入。Bridge 随后被 Brotli 压缩并以 AES-256-GCM 认证封装成单条目的 `qqspeed-runtime.bundle`；`prepack`/`predist*` 只做新鲜度与身份校验，不隐式修复制品。

NSIS、Portable 和 `win-unpacked` 只携带 `app.asar` 中的 bundle，不携带裸 Bridge/Hook 或 `runtime-artifacts.json`。运行时在 `%LOCALAPPDATA%\QQSpeedLauncher\runtime\sessions\s-<32hex>` 受限落盘，utility 在 Koffi 加载前复验 Bridge 哈希，Bridge 写出并复验 Hook。GameApp 退出后，独立 PowerShell waiter 只对固定白名单及严格 PID 日志做非递归清理；Launcher 启动时的租约 scavenger 负责安全兜底。由于 Windows 加载/注入需要真实路径，这不是无落盘方案。

## 账号持久化

`launcher-account.json` 只由 Electron main 管理：

- 用户名、最后账号、记住密码/自动登录选项为结构化元数据；
- 账号文件不包含明文密码、密文或 `protectedPassword` 字段；
- 密码由 utility process 加载的 `credential-vault.js` 独占，使用 `CryptProtectData/CryptUnprotectData` 和 `CRYPTPROTECT_UI_FORBIDDEN`，以每账户 DPAPI Base64 保存到 `launcher-credential-vault.json`；
- main 不导入或解析密码保险库；空密码自动重新认证时，main 只发送账号和 remember/auto 元数据；
- 只有私服认证、`ApplyAuth` 和原生 authenticated 确认都成功后才更新保险库，再向 main 发送 `credential-state`；
- 保险库或账号元数据持久化故障只形成 warning，不改变已成功的认证状态；
- 错误密码、超时和服务端拒绝不会覆盖已保存密码；
- 写入使用同目录临时文件后 rename；
- DPAPI 不可用时不降级为明文存储；旧 `safeStorage` 密文读取时会被清除并关闭 remember/auto，需要重新登录一次；
- Electron 模式不依赖 `ASpeed.ini`，成功提交后对旧文件做尽力清理。

## 动态内容与媒体

`launcher-content-service.js` 使用官服 1.0.0.14 身份参数并按组件挂载边界请求内容。bootstrap scope 只以 `POST /get_all_conf` + body `{}` 请求全局配置；认证事件挂载首页后，home scope 才用 `Promise.allSettled` 并行请求 `get_all_conf`、`get_ads`、`get_bulletin` 和 `get_reflinks`。官服在登录后新挂载 ads/bulletin/reflinks sibling hooks，all-conf 通常复用登录前 SWR 数据；当前实现并行刷新四个逻辑 feed，但不建立虚假的串行依赖。服务限制各响应大小、校验 HTTP/业务结果并只接受共享 HTTPS allowlist 的媒体地址。首页部分接口失败时会合并上次已净化的 feed；全部失败时使用 home 缓存，再失败使用本地默认内容。bootstrap 与 home 使用不同缓存文件，避免登录页提前伪装成已登录首页。

renderer 根据结构化数据渲染登录背景、主页 Hero、主/副活动、Banner、公告/赛事页签、时间线和 5 个导航，不执行远端 HTML。窗口严格固定 1601×910；首页主广告 500×255 横向 5 秒轮播、副活动 500×80、timeline popover 208×109、大区入口 252×48、真实大区 modal 890×674、开始按钮 360×80。登录与主页背景支持 MP4 及海报回退；独立 `<audio>` 是私服扩展音乐能力。视频和音乐有各自的静音/音量偏好，媒体不存在或播放失败时不会阻断登录和启动。

`assets/login-bg-default-BpD5IuOu.png` 是从本轮官服隔离 runtime 取得的原始背景层，2,330,512 bytes，SHA-256 `6be4a48d3ff6a19481bf915546d9f24a94be1a4dbb47be929f3e052a9be5e3da`；它只用于动态配置失败/尚未返回时的 fallback，不是包含 UI 的整页截图。

normal font 资源已替换为本机官服 unpacked 游戏字体 `FZLTH_GB18030.TTF` 的有效字节；当前目标文件大小 9,155,616，SHA-256 `498171f93b3cd37f08a1262bcf4774b854e30e2900532773d35c5a8f8e19d98b`。最终 Electron 视觉采集确认字体已加载，console/http/OTS 错误均为 0。

生产代码和交付资产均不再使用 `home-bg-official-qqspeed.png` 整页截图。

## DirSvrInfo1.ini 删除边界

`DirSvrInfo1.ini` 不在：

- `resources/native/`；
- 已退役并从源码资源目录删除的 `runtime-artifacts.json`；
- `package.json` 的原生 `extraResources`；
- Electron 登录或 Hook 路由读取路径。

安装态只在 `app.asar` 中携带认证 bundle，不裸放桥接 DLL、Hook DLL 或 manifest；随机 session 运行期间才临时形成桥接 DLL 与 Hook DLL。端点由受控模块传给 JS 登录客户端和原生 Hook 配置，不能由玩家侧 INI 改写。

## 验证状态

### 已完成

- 历史摘要根因由三份活动/对应 `MD5.h`、独立 MSVC probe 和 5 组 fixture 交叉确认；1/55/56/64 字节边界及 UTF-8 高位字节 probe 一致；
- `npm test` 为 **178/178 PASS**；语法、native boundary/runtime bundle/credential/lifecycle/endpoint/Fuse/IPC/startup diagnostics 全部通过；
- 锁定端点真实认证得到有效 UIN；隔离 profile 的打包内层回归穿过 `renderer -> main -> utility -> 私服认证 -> ApplyAuth -> native authenticated -> UserLogin -> 主页 -> StartGame -> 同一 GameApp PID ready`；
- 同一回归中的 GameApp 完成预加载与开始，Hook 日志出现 `clean hook loaded`、`network_preflight accepted=1`、`network_hooks install_result exact=1`；ready 后关闭登录器不会误杀运行中的 GameApp，测试随后停止 GameApp 并确认本次随机 runtime session 和进程残留均为 0；
- 最终 1601×910 登录页、主页和大区 modal 视觉采集 **12/12** 断言通过；登录页全图 MAE `3.0735/255`，14 个关键矩形中 11 个精确、最大偏差 3px，字体和背景源哈希未变；
- `npm run pack`、`npm run check:packaged`、`npm run dist:all`、`npm run check:release` 通过；ASAR 共 82 个条目，0 个 DLL entry，只包含一个 `resources/native/qqspeed-runtime.bundle`，`app.asar.unpacked` 仅有 Koffi 的 `koffi.node`；安装树未包含裸 Hook/Bridge、INI、整页截图或旧 C++ 登录源码；
- Hook（29,426,176 bytes）SHA-256 为 `f79c05865271fc91be49f842408b12fe8b61f0ca0f8fc0c1f4b42006560adb74`；含 Hook RCDATA 的 NativeBridge（29,672,448 bytes）为 `5f126f561328ba5a97a8ae9601907bd3a62d26ae7d6ef94a7add0e60fdb24c7d`；最终 bundle（24,751,749 bytes）为 `a7419572d14477d80e5cfd541e89020315fb5706e9992278d8f19ddb8b334fe4`；
- NSIS 实际安装、安装树扫描、安装态启动和卸载均通过；卸载删除当前产品安装目录、快捷方式和 runtime，同时逐字节保留 Roaming 账号元数据、DPAPI vault 与设置。

证据：`F:\new1\analysis\login_hash_audit\REPORT.md`、`F:\new1\analysis\final_visual_capture\REPORT.md`、`F:\new1\analysis\final_package_audit\PACKAGE-AUDIT.json`。

### 仍未扩大解释

- 私服回归已验证 NativeBridge `ready` 契约，但没有执行一局实际游戏、赛道加载或长时间网络稳定性验收；
- 官服成功 SSO、`/pt_login_v2`、登录后主页与官服开始游戏未运行穿越，结论来自活动 bundle 静态证据；
- 旧 Launcher 使用 `GetWindowTextA`，当前 Electron 表单使用 UTF-8，非 ASCII 密码的真实输入编码等价仍未验证；
- 远端内容没有提供视频/音乐 URL 时，只能验证媒体状态机和回退，不能声称 live 媒体已实际播放；
- 安装包未配置 Authenticode 证书，最终状态为 `NotSigned`。
- 当前终端为管理员 token；真实私服登录/StartGame 回归采用显式允许的 packaged inner direct 模式。Portable 外层已通过结构、哈希及临时解包目录清理检查，但未在标准用户 token 下再次穿越完整登录链。
