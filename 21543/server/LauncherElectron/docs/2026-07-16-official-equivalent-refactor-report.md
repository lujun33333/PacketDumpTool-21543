# 2026-07-16 QQSpeed 官服等价重构与逆向对照报告

## 1. 结论

本轮不再以“看起来像”的静态页面作为完成标准，而是先对官服 `1.0.0.14` 的实际 renderer、打包代码和状态机做证据化还原，再据此收敛当前登录器的职责边界：

- 保留 `qqspeed_clean_hook_64.dll` 的功能，但交付时先作为 RCDATA 镶嵌进 `QQSpeedNativeBridge.dll`，再将 Bridge 封装进认证运行时 bundle；
- 私服 TCP/QQ TEA、认证状态机和 GameApp 高层调度由独立 Electron `utilityProcess` 承担；
- C++ DLL 只保留 Hook 注入、GameApp 进程/窗口、共享内存、命名事件和必要 Win32 握手；
- renderer 只在 main 发布权威 `UserLogin` 后进入主页，登录 IPC 返回成功本身不切页；
- 登录页只加载 `get_all_conf`，主页在认证事件后才加载活动、公告和链接 feed；
- `launcher-account.json` 只保存账号策略元数据，密码持久化由 utility process 独占的 Windows DPAPI 保险库负责；
- 固定窗口与主要几何基线改为官服真实的 **1601×910**；
- 生产页面不再使用整页官服截图，保留动态图片/视频背景，并增加可选独立音乐能力；
- `DirSvrInfo1.ini` 已从 Electron 运行时与打包契约移除。

本报告区分“运行态观察”“静态证明”和“尚未运行验证”。**没有执行官服账号登录或官服开始游戏；私服锁定端点已完成认证、`ApplyAuth`、native authenticated、`UserLogin`、主页、显式 `StartGame` 与同一 GameApp PID `ready` 闭环**。报告不记录账号、密码、派生摘要、GameKey、SignKey、token 或 cookie。

## 2. 证据范围与可信度

### 2.1 官服样本与证据包

| 证据 | 路径 | 能证明什么 |
| --- | --- | --- |
| 隔离运行态 | `F:\new1\analysis\official_runtime_exact\REPORT.md` | 真实 1601×910 登录页、DOM/rect/computed style、登录前网络请求和进程树 |
| 精确静态 UI | `F:\new1\analysis\official_static_exact\REPORT.md` | 根组件分支、CSS 几何、动态媒体、登录/主页组件和 SSO 原生输入边界 |
| 状态机 | `F:\new1\analysis\official_state_machine\STATE_MACHINE.md` | SSO→`/pt_login_v2`→`UserLogin`、主页 feed、GameApp preload/start、切换/退出 |
| 机器可读状态机 | `F:\new1\analysis\official_state_machine\state-machine.json` | 状态、迁移、guard、证据位置和运行验证级别 |
| 官服登录截图 | `F:\new1\analysis\official_runtime_exact\official-renderer-login.png` | 未登录真实 renderer 视觉基准 |
| 私服密码摘要审计 | `F:\new1\analysis\login_hash_audit\REPORT.md` | 三份 `MD5.h`、MSVC probe、fixture、运行配置与脱敏认证结果 |
| 最终视觉 | `F:\new1\analysis\final_visual_capture\REPORT.md` | 登录页/主页/modal、几何、字体、console/http 与像素对照 |
| 最终包审计 | `F:\new1\analysis\final_package_audit\PACKAGE-AUDIT.json` | ASAR、extraResources、运行时哈希、移除项与兼容摘要实现存在性 |

证据级别：

- `RUNTIME_PROVEN`：在隔离官服副本中实际观察；
- `STATIC_PROVEN`：由当前活动 bundle、CSS、preload 或 main 代码直接证明；
- `RUNTIME_UNVERIFIED`：代码路径存在，但本次没有用有效账号实际穿越。

### 2.2 官服运行态边界

隔离运行没有复制官服真实 AppData、cookie、Local Storage 或账号历史；全程未输入或提交凭据。运行时只观察到登录页 `POST /basesrv/get_all_conf`，未出现 `GameApp*`。因此登录后主页、`/pt_login_v2` 成功和游戏开始链均不能写成官服运行态已验证。

## 3. 官服 1.0.0.14 的精确做法

### 3.1 根结构与固定几何

官服 renderer 根分支为：

```text
kh
├─ ph                   全窗图片/视频背景，始终存在
├─ Bp                   顶栏与底栏共享壳，始终存在
├─ wh                   升级弹窗
└─ loginInfo ?
   ├─ dh + $h           登录后主页与子账号弹窗
   └─ cm                未登录页
```

关键运行态/静态一致几何：

| 区域 | 官服值 |
| --- | --- |
| BrowserWindow / viewport | `1601×910`，无边框，不可缩放 |
| 顶栏 | `0,0,1601×70` |
| 底栏 | `y=778`，高 `132` |
| 登录背景可视区 | `0,0,1171×910` |
| 右侧登录栏 | `x=1171`，宽 `430`，高 `910` |
| 登录栏样式 | `#312b7466`、`1px rgba(207,229,255,.2)`、`blur(20px)` |
| 登录内容 | `x=1207,y=109,w=358` |
| 主登录按钮 | `1207,442,358×70` |
| 扫码/快捷登录 | `358×56`，`y=534/600` |
| 未登录开始按钮 | `1206,804,360×80`，文案“登录后开始游戏”，禁用 |
| 主页左列 | 宽 `500`；主广告 `500×255`；副活动 `500×80` |
| 大区入口/弹窗 | `252×48` / `890×674` |
| 时间线浮层 | `208×109` |

精确来源：`F:\new1\analysis\official_runtime_exact\key-layout.json` 与 `F:\new1\analysis\official_static_exact\ui-geometry.json`。

### 3.2 登录按钮到主页不是一次函数返回

官服当前配置是 `QQLoginType.SSO`。HTML password input 被禁用，真实密码编辑器由 `ssologin.exe` 原生窗口覆盖；Electron 只传 UIN、remember 和 auto-login 策略。

精确顺序：

1. renderer 校验 UIN、协议勾选和重复提交，设置 loading；
2. renderer 隐藏原生密码窗，通过 preload 调 `QQAccountLogin(uin, rememberPassword, isAutoLogin)`；
3. main 发布 `BeginLogin`，调用 `ssologin.exe` 的 SSO RPC；
4. 原生阶段等待 success/reject/failure/60 秒 timeout；
5. 原生 success 后 main 构造 `PT_LOGIN` 类型的 `loginInfo`；
6. main 请求登录服务 `POST /pt_login_v2`，并处理阻断型 `hope_instructions`；
7. 只有后端成功后才提交 `loginState={isLogin:true,loginInfo}` 和账号元数据；
8. main 继续处理特殊登录类型、token refresh、websocket，并发布 `EndLogin`；
9. main 最后发 `UserLogin(loginInfo)`；
10. EventBus/IPC/preload 把事件送到 renderer；
11. renderer 写入 `loginInfo`，React 才从 `cm` 切到 `dh + $h`；
12. 主页组件挂载后才启动其内容 hooks；游戏仍不会因登录成功自动开始。

因此两个硬约束是：

- `QQAccountLogin` 返回 `code=0` **不是**页面路由事件；
- 必须显式允许“main 已提交、renderer 尚未收到 `UserLogin`”这一短暂状态。

### 3.3 登录页与主页内容时序

登录页根背景 `ph` 已挂载 `getAllConfFrontend()`：

```text
POST /get_all_conf
body: {}
header: headerBuilderNoAuth
```

本次官服隔离运行实际只观察到这个登录前请求。`loginInfo` 写入后，主页的 sibling React/SWR hooks 独立挂载：

- `getAds`；
- `getBulletin`；
- `getReflinks`；
- `getAllConf` 作为第四个逻辑 feed 通常复用登录页已经建立的 SWR 数据，也可能按 SWR 策略重新校验。

官服没有 `ads -> bulletin -> reflinks` 的串行 await 顺序，网络到达顺序也不保证。当前重构按相同挂载边界拆成 bootstrap/home 两个 scope：bootstrap 只发 `get_all_conf {}`；权威 `UserLogin` 后，home scope 用 `Promise.allSettled` 并行聚合 `get_all_conf/get_ads/get_bulletin/get_reflinks`，单 feed 失败不阻断其他 feed。

### 3.4 GameApp：预加载不等于开始游戏

官服 `enablePreload=true`：

1. 游戏状态 Ready、无更新时，`startGameProcess(true)` 以 `--preload-game` 启动 GameApp；
2. 此时 GameSdk 仍是 `NotRunning(0)`，renderer 不能显示“游戏中”；
3. 点击“开始游戏”后才执行 login/maple/game-account/permission/launcher-version/game-version 等 guard；
4. guard 全部通过才进入 `Starting(1)`；
5. 已存在的预加载 PID 被复用，并完成 `GameEventCheckClickStartGame`、管道/窗口/RPC 握手；
6. 完成后才进入 `Running(2)`。

当前私服实现用 `BeginSession -> preloaded -> login/ApplyAuth -> authenticated -> StartGame -> starting -> ready` 复现“预加载、认证、开始门分离”的可观察语义，但底层私服 ABI 并不是官服 MSDK 管道。

### 3.5 切换账号、logout 与关闭是不同路径

- 官服可见“切换账号”：`cancelAutoLogin()` 后 `app.relaunch()`，不是普通 logout；
- token 过期、密码变更或明确退出登录：清 token/ws/native session，发 `UserLogout`，renderer 清 `loginInfo` 回登录页；
- 关闭窗口：先遵守最小化到托盘/退出偏好，再检查 starting/running/migrating 游戏，确认后才退出。

## 4. 当前重构实现

### 4.1 Electron/utility/C++ 边界

```text
renderer
  表单、媒体、auth × game × content UI 状态
        │ 可信 preload IPC
        v
Electron main
  窗口、会话提交、账号元数据、UserLogin/UserLogout、内容服务
        │ utilityProcess message
        v
JS utility process
  DPAPI owner、私服 TCP/QQ TEA、状态机、Koffi 调度
        │ BeginSession / ApplyAuth / StartGame / Cancel
        v
C++ NativeBridge DLL
  GameApp Win32 生命周期、Hook、共享内存、事件、窗口/WM_COPYDATA 握手
```

renderer 的 `official-launcher-state.js` 将 `auth × game × content` 建模为正交区域。登录 IPC 成功只表示命令完成；main 完成私服认证、原生 `ApplyAuth`、凭据策略和 session commit 后才发布 `launcher:user-login`，renderer 收到该事件（或启动时拿到权威 session snapshot）才进入主页。

当前状态一致性约束还包括：

- main 和 worker 都在第一次 `await`/dispatch 前取得独占 login owner；重复登录不会改写首个 attempt 的状态、waiter 或 secret redactor；
- 未认证 IPC 即使请求 `home` 也只允许 bootstrap scope；登录和退出都会失效旧 home Promise，重新登录必须重新取得四个 feed；
- 切换账号先等待 native cancel 成功，只有成功后才清 main session、home cache 并发 `UserLogout`；cancel 失败保留原主页/会话并显示错误；
- GameApp 退出或 native `failed/cancelled` 时，只有确实存在 remembered credential 才保留 launcher session；否则清 session/home cache 并只发一次 `UserLogout`，避免无密码的第二次启动卡死；
- 每次开始游戏前重新执行 `prepareGameApp`，不只信 renderer 按钮的旧状态。

### 4.2 私服认证与凭据边界

- `src/main/login/private-login-client.js`：TCP、timeout/cancel、半包/粘包和错误映射；
- `src/main/login/private-protocol.js`：CSHEAD/MSGHEAD framing，以及历史 Launcher/Server `MD5.h` 的 wire-compatible 摘要；
- `src/main/login/qq-tea.js`：QQ TEA；
- `src/main/native-worker.js`：唯一执行私服认证并调原生 ABI 的 utility process；
- `src/main/credential-vault.js`：utility-only Koffi/Windows DPAPI owner。

`launcher-account.json` v5 只保存 username、remember/auto 和 last-login metadata，不保存密码明文、密文或 `protectedPassword`。`launcher-credential-vault.json` 的每账户密码由 `CryptProtectData/CryptUnprotectData` 保护；main 不导入或解析保险库。损坏格式在读取时 fail closed，后续合法 store/remove 可原子修复；真实 I/O 故障不伪装为“无凭据”。

只有私服认证成功、`ApplyAuth` 成功且原生到达 authenticated 后，utility 才更新或删除保险库。持久化失败不回滚已经成功的会话，但会关闭 remember/auto 并向 UI 返回 warning。旧 `safeStorage` 的 `protectedPassword` 因 owner 变更无法迁移，升级时清除并要求重新输入。

原“密码错误”的根因是使用标准 Node MD5，而活动服务端 `passEncryp: 1` 要求历史 C++ `MD5.h` 的非标准结果。当前 JS 精确复刻四项行为：常量表首项额外为零并导致后续常量错位、把 padded byte count 当作 `uint32` 元素数扩容、MSVC signed-char 对高位字节符号扩展、最终直接拼接 `A/B/C/D` 四个状态字。三份 C++ 头文件、独立 MSVC v143 probe 和 5 组 fixture 一致；明文、RFC MD5 和只做字节序交换都不是正确 wire 值。

边界差异必须明确：私服 renderer 仍会短暂取得用户输入并经 main 一次性传给 utility；官服的真实密码输入属于 `ssologin.exe` 原生边界。当前锁定端点下 `SignKey` 是上述 wire-compatible 摘要，它在认证成功后仍须短暂穿过 C++，因为旧 `KartMap_Shared` ABI 要求该字段；C++ 不负责计算、认证或持久化它。临时脱敏器同时覆盖原始密码、RFC MD5、兼容摘要、`SignKey` 与 `GameKey`。

### 4.3 C++ 清理与保留例外

原生项目是 DLL-only，业务代码仅编译：

```text
NativeBridge.cpp
NativeBridgeIpc.cpp
NativeBridgeLog.cpp
NativeBridgeProcess.cpp
```

另由 `HookPayload.rc`/`resource.h` 将固定身份的 Hook 作为 RCDATA 资源 `201`（LANGID `0x0409`）链接进 Bridge；这不把登录协议或状态机迁回 C++。

已移出/删除的职责包括旧 `PrivateLogin`、QQ TEA/MD5 登录算法、Winsock 登录、锁定登录端点、EXE/`wWinMain`、注册表和无业务作用的旧分支。保留：

- `KartMap_Shared_<launcher pid>`、`RunSpeedGameEvent_<launcher pid>`；
- `SpeedHookCfg_<GameApp pid>`；
- GameApp 创建、进程/窗口监控、Hook 注入、WM_COPYDATA/共享内存握手；
- ABI layout `static_assert` 与清理时 `SecureZeroMemory`；
- GDI loader UI，作为仍需 Win32 的视觉能力例外。

`SharedData.PwdMD5` 名称仍存在于兼容结构中只是固定 ABI 字段，不表示 C++ 仍实现 MD5 登录。

当前运行时制品身份：

| 文件 | 大小 | SHA-256 |
| --- | ---: | --- |
| `resources/native/qqspeed_clean_hook_64.dll` | 29,426,176 | `f79c05865271fc91be49f842408b12fe8b61f0ca0f8fc0c1f4b42006560adb74` |
| `qqspeed-runtime.bundle` 解码出的 Bridge（已含 Hook RCDATA，不在源码资源目录持久保留） | 29,672,448 | `5f126f561328ba5a97a8ae9601907bd3a62d26ae7d6ef94a7add0e60fdb24c7d` |
| `resources/native/qqspeed-runtime.bundle` | 24,751,749 | `a7419572d14477d80e5cfd541e89020315fb5706e9992278d8f19ddb8b334fe4` |

Hash 由构建校验器和本地文件重算交叉确认。NativeBridge 的编译、链接均启用 `/Brepro`；构建器还解析 PE 资源并校验镶嵌 Hook 的长度和固定 SHA-256。Hook 保持原制品不变，不从外部 Hook 源码树隐式覆盖。Bridge 随后以 Brotli + AES-256-GCM 封装成只含一个精确条目的认证 bundle；客户端内置密钥只能提高随手提取门槛，不能对本机逆向者承诺不可恢复。

启动时只把 Bridge 释放到 `%LOCALAPPDATA%\QQSpeedLauncher\runtime\sessions\s-<32 hex>`；Bridge 再从自身资源释放 Hook。两者均在加载/注入前重读校验，退出后由受限白名单清理器删除。Windows `LoadLibrary`/远程注入在当前方案中仍要求真实文件路径，因此这是短生命周期、随机目录、可核验清理的临时落盘，不是“无盘加载”。

### 4.4 页面、字体、视频和音乐

- BrowserWindow/CSS 固定 `1601×910`，不再使用自适应缩放伪造比例；
- 登录页右侧 430px 玻璃栏、358×70 登录按钮和底部 360×80 按钮按官服精确几何实现；
- 主页使用独立 Banner、版本条、公告 tab、时间线、大区 modal 和开始按钮，不使用整页截图；
- Banner 是 `500×255` 横向轮播，默认 5 秒；副活动为 `500×80`；
- 大区 modal 只展示实际存在的私服区，不伪造官服多区；
- 登录/主页背景支持配置驱动的图片或 MP4、poster fallback、静音和音量；
- 独立 `<audio>` 音乐是私服扩展，只有动态配置提供 URL 时才启用。官服本身的背景声音主要属于背景视频音轨。

常规 `preloaded/等待登录` 状态会清除登录 loading toast；主页隐藏 info/ok/loading 这类过程提示，但 warn/error 仍持续可见，避免用调试状态条破坏官服首屏，同时不吞掉真实失败。

生产 fallback `assets/login-bg-default-BpD5IuOu.png` 已替换为本轮官服隔离运行真实取得的原始登录背景 `F:\new1\analysis\official_runtime_exact\official-login-background.png`：2,330,512 bytes，SHA-256 `6be4a48d3ff6a19481bf915546d9f24a94be1a4dbb47be929f3e052a9be5e3da`。它只在动态配置尚未返回或加载失败时作为**背景层**，不是包含导航、公告、按钮的整页 UI 截图。

原先在当前 Electron 版本下被 OTS 拒绝的 normal font 已替换为官服本机 unpacked 游戏字体 `FZLTH_GB18030.TTF` 的有效字节，并保持 renderer 现有资源名：

| 项目 | 值 |
| --- | --- |
| 源 | `F:\QQ飞车\QQSpeed_Launcher\1.0.0.14\resources\app.asar.unpacked\game\font\FZLTH_GB18030.TTF` |
| 目标 | `src/renderer/assets/方正兰亭黑-DEzcQz3Q.TTF` |
| 大小 | 9,155,616 |
| SHA-256 | `498171f93b3cd37f08a1262bcf4774b854e30e2900532773d35c5a8f8e19d98b` |

源/目标哈希一致。最终 Electron 截图中的 font load 与像素差仍列入发布前视觉复验，不把文件一致性替代成运行时视觉已通过。

### 4.5 删除整页截图与 `DirSvrInfo1.ini`

- `assets/home-bg-official-qqspeed.png` 已删除，生产 renderer 无引用；
- `DirSvrInfo1.ini` 不在 `resources/native`、runtime manifest、`extraResources` 或活动登录/Hook 路由读取链；
- 安装态只在 `app.asar` 内交付 `resources/native/qqspeed-runtime.bundle`，不存在裸 `QQSpeedNativeBridge.dll`、裸 `qqspeed_clean_hook_64.dll`、`resources\Native` 或 `runtime-artifacts.json`；运行时才在随机 session 中临时形成 Bridge/Hook，并在 GameApp 退出后清理。

该结论只覆盖当前 Electron 登录器。官服提取出的活动 Electron assets 也没有引用 `DirSvrInfo1.ini`，但这不能证明所有历史工具或独立游戏客户端永远不读取任何同名 INI。

## 5. 官服一致点与明确差异

| 维度 | 已对齐 | 仍不一致/原因 |
| --- | --- | --- |
| 页面路由 | 以权威 `UserLogin/UserLogout` 驱动，不以按钮返回码硬切 | 官服事件来自 SSO/PT；私服事件来自 utility/ApplyAuth commit |
| 认证阶段 | 表单→pending→main commit→renderer event 分层 | 官服是 `ssologin.exe + /pt_login_v2`；私服是 TCP/QQ TEA |
| 密码落盘 | main 只存元数据；秘密材料归独立 owner | 官服 owner 是 SSO native；私服 owner 是 utility DPAPI，输入仍短暂经过 renderer/main |
| 内容 | 登录前配置，登录后主页 feeds；页面组件化 | 当前 home scope 会并行刷新四接口；官服 fresh SWR 通常复用登录前 all-conf |
| GameApp | 预加载、认证、点击开始分开 | 官服使用 MSDK/`rawInfoJson`/`--preload-game`；私服使用旧共享内存和命名事件 |
| Hook | 生命周期、嵌入资源和 hash 受控；安装态不裸放 DLL | 私服必须保留网络重定向 Hook，官服不使用此兼容层；Windows 注入期间仍需临时文件 |
| 视觉 | 1601×910、430px 登录栏、主页关键几何和动态背景 | 不是执行官服 React bundle；登录后官服运行态未抓取，不能声称逐像素全页相同 |
| 大区 | 有真实选择器/modal 与状态 | 当前只有一个真实私服区，未复制官服发现/测速/维护服务 |
| 切换账号 | 与 logout 分离 | 官服 relaunch；当前实现取消并重建同进程 utility/native session |
| QR/快捷 | UI 状态明确 | 私服 QR 不支持；快捷登录只使用 DPAPI 已记住凭据 |
| 音乐 | 媒体有独立状态与控制 | 独立音乐是私服扩展，不是官服同一实现 |

其他未对齐项：官服完整升级/修复/迁移/托盘退出 guard 尚未全部复制；没有正式代码签名证书；允许的远端媒体域名和安全渲染策略比官服 bundle 更收敛。

## 6. 当前验证证据与不能扩大解释的部分

### 6.1 已有直接证据

- 官服隔离副本：真实 1601×910 登录页、disabled HTML password input、`ssologin.exe/QRLoginDlg.exe`、登录前 `/get_all_conf`；
- 官服静态 bundle：SSO→`/pt_login_v2`→commit→`UserLogin`、主页 sibling hooks、preload/start、switch/logout/close；
- 当前源码：utility/C++ 边界、DPAPI owner、组件化页面、固定几何、无整页截图生产引用；
- 当前文件：NativeBridge/Hook/runtime bundle/font 的大小和 SHA-256 与构建校验或源文件一致；
- 当前 fallback 背景与本轮官服 runtime 原始背景的大小和 SHA-256 一致，且只作为 `ph` 等价背景层；
- 历史摘要独立审计：三份对应 `MD5.h`、MSVC v143 probe、5 组 fixture 和活动 `passEncryp: 1` 配置交叉一致；
- 私服真实打包回归：隔离 profile 中从认证 bundle 建立随机 runtime session，取得 GameApp `preloaded`、Hook 三个决定性标记、有效 UIN、`ApplyAuth`、native `authenticated`、main session commit、`UserLogin`、主页、显式 `StartGame` 与同一 PID `ready`；Launcher 退出时 ready GameApp 保持运行，测试停止 GameApp 后本次 session 自动消失；输出含 `startInvoked=1 ready=1 runtimeSessionCleaned=1`；
- 最终视觉：登录页、主页、大区 modal 共 12/12 断言通过；登录页全图 MAE `3.0735/255`、背景 artwork MAE `1.65/255`，14 个关键矩形 11 个精确且最大偏差 3px；字体已加载，console/http/黑块均为 0；
- 最终制品：178/178 单元测试、所有安全/运行 smoke、packaged smoke、release smoke 通过；ASAR 扫描为 82 entries、0 个 DLL entry，`app.asar.unpacked` 仅有 Koffi 的 `koffi.node`，安装树没有裸 Hook/Bridge、INI 依赖、整页截图或旧 C++ 登录源码。

### 6.2 明确未验证

- 未执行成功官服登录，`/pt_login_v2` success、`UserLogin` 和登录后主页均为静态证明、运行未验证；
- 未点击官服“开始游戏”，官服预加载复用与 RPC 握手运行未验证；
- 未逆向 `ssologin.exe` 的密码落盘格式，只证明 Electron/native 职责边界；
- 私服真实回归验证到 NativeBridge `ready`，但没有执行一局实际游戏、赛道加载或长时间网络稳定性验收；
- 旧 Launcher 使用 `GetWindowTextA`，Electron 使用 UTF-8，非 ASCII 密码的真实输入编码等价尚未验证；
- 动态内容若没有视频/音乐 URL，只能验证控制逻辑，不能声称 live 媒体已播放；
- 当前安装包没有正式 Authenticode 证书，状态为 `NotSigned`；
- 当前终端是管理员 token，因此真实私服登录/启动采用已显式允许的 packaged inner direct 模式；Portable 外层已完成结构、哈希和临时解包目录清理验证，但没有在标准用户 token 下再跑一次完整私服登录；
- 官服完整升级/修复/迁移、托盘退出和多区发现/测速/维护服务没有全部复制。

## 7. 两次预览误触发 GameApp 的验证例外

以下事件发生在早期预览调试，不作为通过证据：

1. 一次隔离参数不完整的预览读取了当前应用 userData，触发已有配置的自动预加载；发现后立即停止并清理，没有提交登录、没有点击开始、没有输入凭据。
2. 一次 packaged 视觉预览读到指向另一游戏目录的设置，意外预加载 GameApp；预览被关闭/取消，残留 PID `23880` 被强制停止，同样没有登录或凭据操作。

后续官服精确隔离采集与分析用 mock 视觉采集均要求 `GameApp=0`。最终发布前的全局检查确认 `GameApp.exe`、`electron.exe`、本项目 `QQSpeedLauncher.exe` 均无残留。

## 8. 发布前最终门禁

| 门禁 | 当前报告状态 |
| --- | --- |
| JS 全量单元测试与语法检查 | **PASS**：178/178，`npm run check` 通过 |
| native boundary/runtime manifest/credential/lifecycle/endpoint/Fuse/IPC/startup checks | **PASS** |
| 私服真实登录到主页 | **PASS**：preloaded + Hook + 有效 UIN + ApplyAuth + authenticated + UserLogin + home |
| 点击开始与 GameApp ready | **PASS**：显式 StartGame，同一 PID，`startInvoked=1 ready=1` |
| 登录页、主页、大区 modal 最终截图与 1601×910 几何对照 | **PASS**：12/12，见 `F:\new1\analysis\final_visual_capture\REPORT.md` |
| normal font 在最终 Electron 中无 OTS error | **PASS**：font loaded，console/http/OTS error=0 |
| `npm run pack`、packaged smoke、`npm run dist`、release smoke | **PASS** |
| ASAR/安装态无裸 Hook/Bridge、INI、整页截图和旧 C++ 登录残留 | **PASS**：82 entries、0 DLL entry、仅一个 `qqspeed-runtime.bundle`；无原生 `extraResources` |
| 安装包路径、大小、SHA-256、Authenticode 状态 | `F:\PacketDumpTool\21043版本\MysqlCodes-1.5\LauncherElectron\dist\QQSpeedLauncher-Setup-1.0.0-x64.exe`；297,498,846 bytes；`535805c157aee096f9e30702a22253cf8a678fd6b2b12bb97c74ae87c5e65aa8`；`NotSigned` |
| Portable 路径、大小、SHA-256、Authenticode 状态 | `F:\PacketDumpTool\21043版本\MysqlCodes-1.5\LauncherElectron\dist\QQSpeedLauncher-Portable-1.0.0-x64.exe`；296,835,874 bytes；`178a503f6d52223076f749a5a0eab709778902121caf0ee9f5c76b7c658b67b4`；`NotSigned` |
| 实际安装/卸载 | **PASS**：默认 per-user 安装树扫描和安装态启动通过；卸载后安装目录、当前产品快捷方式和 runtime 删除，Roaming 账号/DPAPI vault/settings 逐字节保留 |
| 退出后 GameApp/Electron/Launcher/runtime session 残留 | **PASS**：0 |

## 9. 主要改动文件

### Electron / JS

- `src/main/index.js`
- `src/main/native-core.js`
- `src/main/native-worker.js`
- `src/main/session-state-machine.js`
- `src/main/credential-policy.js`
- `src/main/credential-vault.js`
- `src/main/launcher-content-service.js`
- `src/main/locked-endpoint.js`
- `src/main/transient-secret-redactor.js`
- `src/main/login/qq-tea.js`
- `src/main/login/private-protocol.js`
- `src/main/login/private-login-client.js`
- `src/preload/preload.cjs`

### Renderer

- `src/renderer/index.html`
- `src/renderer/styles.css`
- `src/renderer/app.js`
- `src/renderer/content-view.js`
- `src/renderer/media-controller.js`
- `src/renderer/official-launcher-state.js`
- `src/renderer/assets/方正兰亭黑-DEzcQz3Q.TTF`

### Native / runtime

- `native/NativeBridge/NativeBridge.cpp`
- `native/NativeBridge/NativeBridge.h`
- `native/NativeBridge/NativeBridgeIpc.cpp`
- `native/NativeBridge/NativeBridgeLog.cpp`
- `native/NativeBridge/NativeBridgeProcess.cpp`
- `native/NativeBridge/NativeBridgeState.h`
- `native/NativeBridge/NativeBridge.vcxproj`
- `native/NativeBridge/HookPayload.rc`
- `native/NativeBridge/resource.h`
- `resources/native/qqspeed_clean_hook_64.dll`
- `resources/native/qqspeed-runtime.bundle`

### 测试、构建与文档

- `test/unit/*.test.js`、`test/unit/*.test.mjs`
- `src/scripts/smoke-portable-runtime-x64.cjs`
- `src/scripts/build-native.cjs`
- `src/scripts/build-runtime-bundle.cjs`
- `src/scripts/build-portable-x64.cjs`
- `src/scripts/smoke-runtime-artifacts.cjs`
- `src/scripts/smoke-portable-x64.cjs`
- `src/scripts/smoke-packaged.cjs`
- `src/scripts/smoke-release.cjs`
- `src/scripts/smoke-locked-endpoint.cjs`
- `src/shared/native-runtime-bundle.js`
- `src/main/native-runtime-cleaner.js`
- `src/build/installer.nsh`
- `README.md`
- `docs/ARCHITECTURE.md`
- `docs/GLAUNCHER-COMPATIBILITY.md`
- `docs/2026-07-16-official-equivalent-refactor-report.md`

按用户要求，本轮直接修改，没有为这些文件创建额外备份。
