# QQSpeedLauncher

QQ飞车 Electron 登录器。当前采用“Hook 最小保留 + Electron 官服等价分层”：私服协议与会话状态机在独立 `utilityProcess` 中运行，C++ DLL 只承担 GameApp/Hook/共享内存等必须依赖 Windows 的能力。

## 运行

```powershell
cd F:\PacketDumpTool\21043版本\MysqlCodes-1.5\LauncherElectron
npm install
npm start
```

程序会定位 QQ飞车主目录并检查 `Releasephysx27_64\GameApp.exe`。自动定位失败时，应选择 QQ飞车主目录，而不是 `Releasephysx27_64` 子目录。

## 当前启动与登录链

```text
打开登录器
  -> Electron main 创建独立 utilityProcess
  -> JS utilityProcess 调用 QQSpeedNativeBridge_BeginSession
  -> C++ 创建 KartMap_Shared / RunSpeedGameEvent，启动并注入 GameApp
  -> GameApp 保持预加载等待

提交登录
  -> JS utilityProcess 执行私服 CSHEAD/MSGHEAD + QQ TEA 登录
  -> 成功响应得到 Uin / 16-byte GameKey / SignKey
  -> 调用 QQSpeedNativeBridge_ApplyAuth 写入共享内存
  -> GameApp 继续等待，不会因登录成功自动进入

点击“开始游戏”
  -> JS utilityProcess 调用 QQSpeedNativeBridge_StartGame
  -> C++ 触发 RunSpeedGameEvent
  -> 等待同一个 GameApp 进程到达 ready
```

JS 状态机明确区分 `preloading -> preloaded -> authenticating -> authenticated -> starting -> ready`。可恢复的认证失败会回到 `preloaded`，允许重试，不会把错误密码写入记住密码存储。

main 与 worker 都对登录 attempt 做前置互斥；未认证请求不能取得 home feed。切换账号只有在 native cancel 成功后才清会话；GameApp 退出或 native 终态时，未记住密码的账号会收到一次 `UserLogout` 回登录页，已记住账号才可保留 session 并为下一次开始自动重建认证。

## 账号与密码存储

- 账号元数据保存在 Electron 用户数据目录的 `launcher-account.json`。
- `launcher-account.json` 只保存账号、`rememberPassword`、`autoLogin` 和最后登录时间，不包含密码或密码密文。
- “记住密码”的密文由独立 utility process 独占管理，使用 Windows DPAPI 后写入 `launcher-credential-vault.json`；Electron main 不导入、不解析该保险库。
- 只有私服认证、`QQSpeedNativeBridge_ApplyAuth` 和原生 `authenticated` 确认都成功后，utility process 才写入或删除保险库；认证失败不覆盖上一次成功密码。
- DPAPI 或保险库落盘失败不会把已成功认证改判成失败：本次仍可开始游戏，但返回 warning，并关闭记住密码/自动登录元数据。
- 从旧版升级时，`launcher-account.json` 中无法跨进程解密的 Electron `safeStorage` 字段会被原子清除，同时关闭记住密码/自动登录；需要重新输入一次密码。
- “切换账号”会先取消当前 utility/native 会话，再重新预加载同一个游戏目录，不需要退出登录器。
- 新输入的凭据只通过进程 IPC 送入一次性 utility process，不放入命令行或环境变量；自动重新认证由 utility process 内部解开 DPAPI 密文，main 不取得已保存密码；临时字符串和 `GameKey` 在使用后做尽力清理。
- 当前锁定端点的 `passwordMode: 'md5'` 指历史 Launcher/Server 的兼容摘要，并不是 RFC MD5：旧实现存在首轮零常量、`vector<unsigned int>` padding 扩容错误、MSVC signed-char 扩展和 `A/B/C/D` 状态字直接拼接四项行为。utility process 精确复刻该 wire 摘要并把它作为 `SignKey` 短暂传给原生桥；原始密码、标准 MD5、兼容摘要、`SignKey` 和 `GameKey` 均进入临时脱敏集合，不记录、不持久化。
- Electron 模式不再依赖旧 `ASpeed.ini`；成功登录提交新存储后会尽力清理遗留账号文件。

## 动态官服内容、视频与音乐

`src/main/launcher-content-service.js` 按官服 renderer 的真实挂载边界分两段请求：登录页只以 `POST` + 精确空对象 body `{}` 请求 `get_all_conf`；main 发布权威 `UserLogin`、renderer 切入首页后，才并行聚合 `get_all_conf`、`get_ads`、`get_bulletin` 和 `get_reflinks`。官服本身在登录后新挂载的是 ads/bulletin/reflinks 三个 sibling hooks，all-conf 通常复用登录前 SWR 数据；当前实现将四个逻辑 feed 在 home scope 并行刷新，不伪造串行顺序。响应被归一化为登录背景、主页主视觉、主/副活动、Banner、公告/赛事页签、版本时间线和 5 个导航链接。首页单接口失败时保留其他实时结果和上次已净化数据，全部失败时才依次回退缓存和本地默认内容。

- 登录页左侧和主页主视觉都支持图片或 MP4 视频，视频失败自动退回海报/默认图片。
- 窗口固定为官服运行态实测的 1601×910；登录页右栏 430px，首页为顶部导航、500×255 主活动、500×80 副活动、公告、版本轴、252×48 大区入口和 360×80 开始按钮；所有区块由 DTO 独立渲染，不使用整页截图。
- 视频声音与独立背景音乐分别提供静音和音量控制，偏好存入 renderer `localStorage`。
- 音乐源来自动态内容配置；当前不捆绑固定音乐文件，配置没有音乐时相关控件自动禁用。
- 默认背景 fallback 使用本轮官服 runtime 提取的原始背景图（SHA-256 `6be4a48d3ff6a19481bf915546d9f24a94be1a4dbb47be929f3e052a9be5e3da`），它只是背景层，不包含任何导航、公告或按钮 UI。
- 生产页面不再引用整页官服截图；`home-bg-official-qqspeed.png` 已从交付资源移除。
- normal font 已换为本机官服 unpacked 游戏字体 `FZLTH_GB18030.TTF` 的有效字节，目标资源 SHA-256 为 `498171f93b3cd37f08a1262bcf4774b854e30e2900532773d35c5a8f8e19d98b`。
- 远程媒体只接受共享 HTTPS allowlist，并在主进程、renderer 与 CSP 三侧使用同一边界校验。

## 原生边界与 Hook

保留 `resources\native\qqspeed_clean_hook_64.dll` 作为**源码树构建输入**，继续负责客户端网络重定向兼容。发布构建会先把它作为固定 `RCDATA`（资源 ID 201、LANGID 0x0409）编入临时 `QQSpeedNativeBridge.dll`，再把 Bridge 以 Brotli + AES-256-GCM 封装为单条目的 `resources\native\qqspeed-runtime.bundle`。构建在 `finally` 中删除临时 Bridge；源码资源目录不再保留裸 Bridge 或 `runtime-artifacts.json`，最终只留下 Hook 构建输入和 bundle。

运行 GameApp 时，Windows `LoadLibraryW`/远程注入仍要求真实文件路径，因此 launcher 会在 `%LOCALAPPDATA%\QQSpeedLauncher\runtime\sessions\s-<32hex>` 创建受限的一次性会话：先认证解包 Bridge，Bridge 再从自身资源写出并逐字节复验 Hook。会话只允许 Bridge、Hook、租约以及严格 PID 命名的 Hook 日志；GameApp 退出后由独立 waiter 有界重试清理，下一次启动另有安全 scavenger 兜底。这是减少安装态明文暴露和持久残留，不宣称“内存无落盘”或能对抗本机逆向。

`QQSpeedNativeBridge.dll` 当前导出：

```text
QQSpeedNativeBridge_BeginSession
QQSpeedNativeBridge_ApplyAuth
QQSpeedNativeBridge_StartGame
QQSpeedNativeBridge_Cancel
QQSpeedNativeBridge_GetState
QQSpeedNativeBridge_GetProgress
QQSpeedNativeBridge_IsBusy
QQSpeedNativeBridge_GetGameProcessId
QQSpeedNativeBridge_GetLastError
```

DLL 不包含私服 socket 登录、QQ TEA、MD5 或协议 framing；也不存在旧 `RunSession`、`SubmitCredentials`、`wWinMain`、C++ 登录窗体或 EXE target。C++ 保留的工作是 Win32 进程创建/监控、Hook 注入、命名共享内存/事件以及 GameApp 必需握手。

renderer 另有一套官服等价 UI 状态机：登录 IPC 的返回值不会直接切首页；只有 main 的 `UserLogin` 事件（或启动时读取到 main 已提交的 session 快照）能把页面从登录态切到首页，`UserLogout` 才能切回。这样避免把“私服 socket 成功”误当成完整会话提交。

校验：

```powershell
npm run verify:runtime
npm run check:native-boundary
```

## DirSvrInfo1.ini

`DirSvrInfo1.ini` 已从 Electron 源码依赖、运行时清单和打包资源中删除。登录与 Hook 地址来自受控运行时配置，不读取该 INI 进行路由。最终包的原生运行时文件只有：

```text
resources\app.asar
  └─ resources/native/qqspeed-runtime.bundle
```

安装目录中不存在 `resources\Native`、`QQSpeedNativeBridge.dll`、`qqspeed_clean_hook_64.dll` 或 `runtime-artifacts.json`；`app.asar.unpacked` 只包含 Koffi 的 `koffi.node`。

这只说明当前 Electron 登录器不依赖 `DirSvrInfo1.ini`；不泛化为所有历史客户端或其他工具都不需要任何 INI。

## 打包单文件 Portable EXE

```powershell
npm run check
npm run dist:portable
```

输出：`dist\QQSpeedLauncher-Portable-<version>-x64.exe`。

- electron-builder 生成 x64 `win-unpacked`；
- `native\portable-x64-bootstrap` 生成 PE32+（Machine `0x8664`）外层；
- 外层只负责校验、释放、启动和退出清理，不承载登录、GameApp、Hook 或服务器业务逻辑；
- 认证 bundle 封装在 Portable EXE 的 `app.asar` 内；不存在裸 Bridge/Hook payload，也不包含兼容 INI。

## 最终验证状态与边界

官服逆向已静态证明 SSO→`/pt_login_v2`→main commit→`UserLogin`、登录后 feed 挂载和 GameApp preload/start 状态机；隔离运行只验证了 1601×910 未登录页与登录前 `get_all_conf`，没有执行官服登录或开始游戏。

私服“密码错误”的根因已定位为错误使用标准 Node MD5。修复后，以隔离 profile 对最终打包内层执行真实凭据回归，已穿过 `renderer -> main -> utility -> 私服登录 -> ApplyAuth -> native authenticated -> UserLogin -> 主页 -> StartGame -> 同一 GameApp PID ready`；Hook 日志同时包含加载、网络预检接受和精确 Hook 安装成功标记。测试关闭登录器后确认 ready GameApp 不被误杀，随后终止 GameApp，并断言本次随机 runtime session 自动消失（`runtimeSessionCleaned=1`），最终进程残留为 0。该证据验证到 NativeBridge 的 `ready` 契约，不扩大成已完成一局实际游戏或官服 MSDK 运行验证。

最终门禁结果：单元测试 **220/220**、语法检查、native/runtime、packaged/release smoke 均通过。服务端业务重编译现在复用独立稳定材料，不绑定服务端 EXE 哈希、盘符或客户端安装目录；客户端时钟偏差不再导致运行材料误拒绝。

当前安装包：

```text
F:\PacketDumpTool\21043版本\MysqlCodes-1.5\LauncherElectron\dist\QQSpeedLauncher-Setup-1.0.1-x64.exe
size: 295,183,988 bytes
SHA-256: 3d1af3140e42b5f83c721c1728b12c5152ea40194c39f47063bdf9f006f67cea
Authenticode: NotSigned
```

本轮 `dist` 只保留上述新 NSIS 安装包，不混放旧安装包或展开目录。

任何测试记录均不得保存账号、密码或认证密钥。完整证据位于 `F:\new1\analysis\final_visual_capture`、`F:\new1\analysis\login_hash_audit` 和 `F:\new1\analysis\final_package_audit`。

详细架构、官服兼容结论及本轮变更见：

- `docs/ARCHITECTURE.md`
- `docs/GLAUNCHER-COMPATIBILITY.md`
- `docs/2026-07-16-official-equivalent-refactor-report.md`
