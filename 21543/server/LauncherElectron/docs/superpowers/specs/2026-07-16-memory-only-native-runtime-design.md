# QQSpeedLauncher 内存态 Native Runtime 设计

日期：2026-07-16

## 目标

将 NativeBridge 与 Hook 从安装目录、ASAR 和临时目录中移除。发布版仅在
`QQSpeedLauncher.exe` 的数字 `RT_RCDATA` 资源中携带经过 AES-256-GCM 认证加密的
双入口 payload。登录成功后，服务端在同一 TCP 连接上通过一次性材料释放本次会话
所需的 content key；Bridge 在 Launcher utility process 内手工映射，Hook 在目标
`GameApp.exe` 内手工映射。

这是一种减少静态暴露和落盘痕迹的架构，不承诺对本机管理员、调试器、内存转储或
被接管的服务端不可恢复。

## 信任与密钥边界

1. 构建端生成 32 字节 content key，只提供给 bundle 构建器和服务端密钥配置；不得
   写入源码、ASAR、EXE 明文、日志或诊断输出。
2. 首次安装运行时生成 RSA-2048 安装身份。私钥用当前用户 DPAPI 封装后写入
   `%LOCALAPPDATA%\QQSpeedLauncher\install-secrets`；只允许当前用户读取。公钥和随机
   install ID 不视为秘密。
3. 传统 `msgId=1` 身份认证成功后，Launcher 在同一连接发送 `msgId=3`，携带协议版本、
   install ID、公钥和 client nonce。
4. 服务端验证该连接已经通过登录，生成 material ID 和短时效材料，把 content key、
   bundle ID、material ID、client nonce、登录 UIN 与过期时间作为一个严格定长明文，
   用安装公钥执行 RSA-OAEP-SHA256 封装。material ID 只能消费一次，且绑定该连接、
   UIN、安装公钥指纹、client nonce 和 bundle ID。
5. Launcher 用 DPAPI 解封安装私钥，通过 RSA-OAEP-SHA256 解包并逐字段验证材料，
   然后认证解密 EXE 内 payload；使用完立即覆写私钥明文、材料明文、content key 与
   Bridge/Hook 原始 Buffer。
6. 服务端不得记录账号密码、content key、共享秘密或未脱敏的一次性材料。

## Payload 格式 v2

- 固定 magic + schema=2 + architecture code + cipher/compression + bundle ID + 随机 nonce。
- 两个数字 entry kind：`1=Bridge`、`2=Hook`；头中不出现 DLL 文件名。
- 每项记录解压尺寸和 SHA-256；整个明文表与 payload 一起 Brotli 压缩。
- 随机 96-bit nonce；头作为 AES-GCM AAD；尾部 128-bit tag。
- 解码器必须要求调用方显式传入 32 字节 content key，不提供固定 key、环境回退或
  legacy v1 fallback。
- 严格验证 magic、schema、架构、entry count、尺寸上限、总长度、认证标签和每项 hash。

## EXE 资源

- `afterPack` 在 Electron fuse 写入后、签名前，用 Win32 UpdateResource API 把 bundle
  写入数字 `RT_RCDATA`/数字 ID。
- `package.json build.files` 排除 bundle；发布 ASAR、resources、安装目录中不得有
  bundle、Bridge DLL 或 Hook DLL。
- packaged runtime 从 `process.execPath` 的 PE resource directory 做有界读取；资源
  缺失、重复、越界或认证失败均 fail closed。
- 开发模式只允许通过显式 test/dev 注入读取 fixture，不得进入 packaged 路径。

## 本地 Bridge 映射

utility process 解析 PE32+，校验 x64，分配 image，复制 headers/sections，应用 DIR64
重定位，解析 imports，注册 x64 unwind table，设置最终页权限，刷新指令缓存，调用
TLS callbacks/DllMain，然后按 RVA 绑定 9 个导出。任一步失败必须逆序回滚；卸载时
调用 detach、删除 unwind table、清零 payload 并释放映像。不得调用 `koffi.load(path)`。

## 远程 Hook 映射

utility 在 BeginSession 前调用 Bridge 的 `SetHookPayload(bytes,size)`；Bridge 立即复制
并持有标准 x64 Hook PE。发现/重启 `GameApp.exe` 后，Bridge 在目标进程中分配映像和
loader context，通过远程 loader 完成 imports、relocations、TLS、unwind、protections
与 DllMain attach。成功后清零远程 loader/context 和本地 Hook 副本；失败则释放所有
远程分配并返回稳定错误码。不得写临时 DLL、不得用路径型 `LoadLibraryW` 注入。

正式发布输入使用固定 SHA256 的 `ReleaseVmp` 保护产物。构建门禁必须同时验证 x64 DLL、
128 MiB 映像上限、原始节范围、VMProtect 入口点、DIR64 重定位、x64 unwind、导入终止、
无延迟导入与无持久静态 TLS；不得将带 `VMProtectSDK64.dll` 导入的未保护中间产物打包。

## 状态机

`SIGNED_OUT -> AUTHENTICATING(msg1) -> MATERIAL_REQUEST(msg3) -> UNLOCKING ->
NATIVE_READY -> STARTING_GAME -> IN_GAME -> CLEANUP -> SIGNED_IN`

- msg1 密码错误仍映射为密码错误。
- msg1 成功但 msg3 不可用映射为 `SERVER_UPGRADE_REQUIRED`，不得误报密码错误。
- msg3 超时、过期、重放、bundle 不匹配或认证失败均不得启动 native session。
- cancel/logout/window close 必须中止 socket、清零材料、终止 utility，并释放本地/远程映像。

## 验证门禁

1. 单元：bundle v2、错误 key/tamper、PE resource round-trip/越界、DPAPI install identity、
   msg1->msg3 顺序/超时/重放/旧服错误、本地与远程 mapper rollback。
2. 构建：x64 native、Electron packaged、NSIS、portable；签名前资源可读且 fuse 保持。
3. 静态：ASAR、安装包展开目录和 installed tree 中无 `*.dll` 自定义载荷、无 bundle、
   无 Bridge/Hook 明文名、无固定 content key。
4. 动态：真实私服账号登录、一次性材料消费、Bridge 映射、GameApp 启动/Hook 映射、
   游戏退出和 launcher 关闭后的进程/句柄/映射清理。
5. 负向：旧服务端、错误密码、错误/过期/重放材料、替换 EXE resource、GameApp 重启。

只有第 4 项在升级后的真实服务端完整复现，才可报告真实端到端完成；loopback fixture
只能证明协议和映射实现，不替代生产链路验证。
