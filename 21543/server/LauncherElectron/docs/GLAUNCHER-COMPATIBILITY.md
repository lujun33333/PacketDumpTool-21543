# 官服 GLauncher 兼容性结论

## 结论

当前目标是“官服等价职责和时序”，不是伪造官服二进制或把私服凭据硬塞进官服 MSDK 载荷。

- Electron 负责 UI、内容、账号策略和高层生命周期；
- 独立 JS utility process 负责私服协议、认证状态机和 GameApp 调度语义；
- C++ 只保留 GameApp/Hook/共享内存所需 Windows 能力；
- 私服仍使用 `KartMap_Shared` 的 `Uin + GameKey + SignKey`，不会假称为官服 `rawInfoJson`。

因此：原先“私服 wire login 仍在 DLL、以后再迁 JS”的描述已经过时；迁移已经完成。仍不等价的是认证载荷与服务端协议本身。

## 官服与当前实现对照

| 维度 | 官服 1.0.0.14 | 当前实现 | 结论 |
| --- | --- | --- | --- |
| 外层 | Electron 主程序 | Electron 主程序 | 职责等价 |
| 登录实现 | 当前配置为 `ssologin.exe` 原生 SSO，成功后 main 再请求 `/pt_login_v2` 并发布 `UserLogin` | 独立 JS utility process 的私服 TCP/QQ TEA + 历史 `MD5.h` 兼容摘要，main 完成提交后发布等价 `UserLogin` | 提交边界/页面事件等价，协议不同 |
| 游戏调度 | JS GameModule 驱动预加载/开始，原生模块提供能力 | JS 状态机驱动 BeginSession/ApplyAuth/StartGame，C++ 提供 Win32 能力 | 时序等价 |
| GameApp 登录载荷 | 命名管道 `GetGameAccountLoginStr` 返回 MSDK `rawInfoJson` | `KartMap_Shared_<launcher pid>` 写入 `Uin + 16-byte GameKey + SignKey` | 不可直接互换 |
| 客户端网络 | 官服网络配置 | 保留 Hook 读取 `SpeedHookCfg_<GameApp pid>` 重定向 | 私服兼容差异 |
| 动态页面 | 登录前 `POST get_all_conf` body `{}`；登录后新挂载 ads/bulletin/reflinks sibling hooks，all-conf 通常复用 SWR | bootstrap 只取 `get_all_conf {}`；权威 `UserLogin` 后 home scope 并行聚合四个逻辑 feed | 挂载边界等价；当前 home 会主动并行刷新 all-conf |
| 凭据存储 | 官服账号体系/原生登录模块 | utility process 独占 Windows DPAPI 保险库；main 只存账号元数据；成功认证后提交 | 职责边界更接近官服，不共享官服登录态 |
| 配置 INI | 官服/历史组件各有自身配置 | Electron 包不含且不读取 `DirSvrInfo1.ini` | 私服路由不依赖 INI |

## 官服实际启动时序与复现方式

反编译显示官服 `GameModule.initialize()` 会先执行 `initGamePreload()`，可运行时通过 `startGameProcess(true)` 以 `--preload-game` 创建 GameApp，但 SDK 仍保持 `NotRunning`；用户点击开始、通过登录/大区/版本/权限检查后才进入 `Starting`，并向已存在的 GameApp 返回开始确认。

当前实现复现这一可观察语义：

1. 打开登录器：utility process 调用 `BeginSession`，预加载并注入 GameApp；
2. 登录：JS 私服客户端认证，成功后调用 `ApplyAuth`，仍不放行；
3. 点击开始：调用 `StartGame`，触发 `RunSpeedGameEvent`，同一进程继续到 ready。

`BeginSession` 中实际的 `CreateProcess`/注入/窗口握手仍由 C++ 完成，因为它们属于 Windows 必需能力；调度顺序、认证重试与开始门的权威在 JS utility process。

## 私服协议为何不能直接换成官服管道

官服 GameApp 期望 MSDK `rawInfoJson`，当前私服服务端返回 `Uin` 和 16 字节 `GameKey`，客户端还需要匹配私服 wire password 的 `SignKey`。锁定端点当前使用的 wire password 是历史 Launcher/Server 的非标准 MD5 兼容摘要，不是明文，也不是 RFC MD5。现有 Hook 只消费网络重定向配置，不实现 `GetGameAccountLoginStr`，也没有可信的 `Uin/GameKey/SignKey -> rawInfoJson` 转换。

当前正确做法是保留载荷差异，但把职责放对位置：

```text
JS utilityProcess
  CSHEAD/MSGHEAD + QQ TEA + TCP + 状态机
        |
        | ApplyAuth(Uin, GameKey, SignKey)
        v
C++ NativeBridge
  KartMap_Shared + 命名事件 + GameApp/Hook Win32 生命周期
```

这比复用一个官服管道名字但返回错误数据更接近真实运行逻辑。

## 本地证据

- 官服外层入口：`F:\QQ飞车\QQSpeedLauncher.exe`
- 官服版本选择：`F:\QQ飞车\launcherConfig.json`
- 官服 Electron 包：`F:\QQ飞车\QQSpeed_Launcher\1.0.0.14\resources\app.asar`
- 官服原生模块：`kernel.node`、`login.node`、`rpckit.node`
- 官服辅助进程：`ssologin.exe`、`downloader.exe`、`upgrader.exe`
- 官服游戏预加载入口：`Releasephysx27_64\GameApp.exe -StartFromLauncher --preload-game`
- 当前 JS 协议：`src/main/login/private-protocol.js`
- 当前 JS 网络登录：`src/main/login/private-login-client.js`
- 当前 JS 状态机与调度：`src/main/session-state-machine.js`、`src/main/native-worker.js`
- 当前共享结构：`native/NativeBridge/NativeBridge.h`
- 当前 Hook 配置映射：`native/NativeBridge/NativeBridgeIpc.cpp`
- 当前动态内容：`src/main/launcher-content-service.js`

原 `native/NativeBridge/PrivateLogin.cpp` 已删除，不能再作为“当前登录响应实现”的证据。当前密码摘要根因及逐位兼容 probe 位于 `F:\new1\analysis\login_hash_audit\REPORT.md`；最终包、视觉和运行证据分别位于 `F:\new1\analysis\final_package_audit` 与 `F:\new1\analysis\final_visual_capture`。没有在报告中记录账号、密码、派生摘要、GameKey、cookie、token 或机器码。

官服决定性证据包位于 `F:\new1\analysis\official_runtime_exact`、`F:\new1\analysis\official_static_exact` 和 `F:\new1\analysis\official_state_machine`。隔离运行没有执行成功账号登录或开始游戏；登录后和游戏状态是当前活动 bundle 的静态证明，不是本次官服运行态通过。

## 当前一致点

- Electron 作为 UI 和流程中枢；
- 打开即预加载、登录只认证、点击开始才放行的三段状态；
- 动态配置驱动页面，而不是整页死截图；
- 原生能力被收敛为明确 ABI；
- 账号密码不进命令行，记住密码由 utility process 使用 Windows DPAPI；`launcher-account.json` 不保存密码密文；
- GameApp 仍由受控 Hook/IPC 链获得私服所需运行环境。

## 当前不一致点

- 私服认证协议和载荷不是官服 MSDK，不能共享官服登录态；
- 当前仅暴露私服电信区，未复刻官服完整区服/大区服务；
- 页面采用自有安全渲染组件和少量从本机官服 1.0.0.14 提取的视觉资产/字体，未复制或执行官服 JS bundle，也未使用整页截图；
- 官服账密输入由 `ssologin.exe` 原生密码窗拥有；私服密码仍需从 renderer 表单经 main 一次性转交 utility owner，只有持久化边界等价；
- 官服可见“切换账号”执行 `cancelAutoLogin + app.relaunch()`；当前私服实现在同一 Electron 进程取消并重建 utility/native session；
- 官服扫码登录可进入原生 SSO；当前私服扫码按钮明确提示不支持，快捷登录仅对 DPAPI 已记住账号可用；
- 视频仅接受允许域名下的 HTTPS MP4；音乐必须由内容配置提供，没有固定内置曲目；
- 未配置正式发布证书；
- 锁定端点的认证、`ApplyAuth`、native authenticated、`UserLogin`、主页、`StartGame` 与同一 GameApp PID `ready` 已完成同一打包内层闭环；没有执行一局实际游戏或赛道加载。

## 实机验证结论与缺口

原“密码错误”不是账号数据问题，而是 Electron 使用了标准 Node MD5，服务端 `passEncryp: 1` 实际要求历史 `MD5.h` 的四个兼容缺陷：首轮零常量、word-vector padding 错误、MSVC signed-char 扩展、`A/B/C/D` 状态字直接拼接。修复后已取得以下同一会话非敏感证据：

1. GameApp `preloaded` 与 PID；
2. Hook 加载、网络预检接受和精确 Hook 安装成功；
3. 锁定私服协议返回有效 UIN；
4. `ApplyAuth` 后 utility/native 状态为 `authenticated`；
5. main 提交 session、发布 `UserLogin`，renderer 切到主页；
6. 登录成功后先确认 GameApp 仍停在 `authenticated`，随后显式调用 `StartGame`；
7. 同一 PID 从 `starting` 到 `ready`，输出 `startInvoked=1 ready=1`；
8. 关闭登录器后 ready GameApp 保持运行，测试再显式清理，最终残留为 0。

因此“密码错误”的协议根因、登录到主页和私服开始门/ready 闭环已经解决。仍未覆盖的是一局实际游戏、赛道加载和长时间网络稳定性；官服成功 SSO/home/start 也仍只具静态证明，没有运行穿越。
