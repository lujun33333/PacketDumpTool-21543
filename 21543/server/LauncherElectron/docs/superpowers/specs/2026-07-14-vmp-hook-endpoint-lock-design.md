# VMP Hook 固定端点与发布制品锁定设计

## 目标

正式发布的 QQSpeed 登录器只能加载指定的 VMProtect Hook DLL。该 Hook 仅将已确认的官服登录入口重定向到 `43.226.61.100:29999`；任何可变配置、共享内存篡改、旧 DLL 替换、未保护 DLL 或端点不一致都不得进入游戏连接路径。

## 范围

- Hook：`F:\PacketDumpTool\21446\新建文件夹\qqspeed_clean_hook`
- 登录器：`F:\PacketDumpTool\21043版本\MysqlCodes-1.5\LauncherElectron`
- 不修改现有游戏服务端协议或服务端代码。
- 不引入旧 C++ EXE、`LauncherKernel.exe`、`QQSpeedLauncher_x64.exe`或旧 Hook 目录复制逻辑。
- 不承诺抵抗内核层透明 NAT、完整实时中继或对 VMProtect 代码的定制补丁；目标是关闭普通配置篡改、DLL 替换和直接复用路径，并显著提高逆向成本。

## 已确认的网络边界

### 官服源地址白名单

仅以下源地址且端口位于 `10000-10005` 时属于登录重定向候选：

- `113.96.13.205`
- `121.14.75.60`
- `120.232.19.139`

### 唯一目标端点

- Host：`43.226.61.100`
- Port：`29999`

源地址白名单与目标端点锁是两个不同概念：前者决定哪些原始连接允许被改写，后者决定改写后唯一允许到达的服务器。

## Hook 设计

### 目标读取与校验

Hook 保留按游戏 PID 命名的 `SpeedHookCfg_<pid>`共享映射作为登录器存在性和配置一致性证据，但不再信任映射中的任意 Host/Port。

项目实际包含四个 x64 配置：`Debug`、`Release`、`ReleasePlain`和`ReleaseVmp`。只有 `ReleaseVmp|x64`定义 `QQSPEED_VMP_PROTECTION`，因此只有该配置启用服务器锁和 VMP/CRC 门禁；`Debug`、`Release`和`ReleasePlain`保持当前本地调试/使用行为，不增加服务器验证。

正式 `ReleaseVmp`流程中：

1. 从共享映射读取 Host/Port。
2. 校验 Host/Port 必须精确等于 `43.226.61.100:29999`。
3. 校验当前进程 PID 与映射名称一致。
4. 调用 `VMProtectIsProtected()`和`VMProtectIsValidImageCRC()`校验 VMProtect 运行状态与映像 CRC。
5. 由 Hook 内部受保护逻辑重新构造固定 IPv4 `sockaddr_in`，而不是直接使用共享映射字节。

任一校验失败时，将锁状态设为拒绝。锁状态不得退化为使用共享配置中的其他地址。

### Fail-closed

对已识别的官服源地址和端口：

- 锁状态有效：连接到固定目标。
- 共享映射缺失、目标不一致、VMP 未生效、CRC 无效或固定目标构造失败：返回 `SOCKET_ERROR`并设置 `WSAEACCES`。
- 禁止回落到官服原地址或共享配置中的其他地址。

对不属于登录重定向候选的连接保持原行为，避免误伤游戏更新、聊天和其他网络服务。

### 实际拦截入口

当前运行入口是 `Network.dll`导入的 MFC `CAsyncSocket::Connect`。固定端点决策必须位于该实际入口的公共受保护决策函数中，不能只修改当前未安装的通用 `connect/WSAConnect`包装函数。

首次启动和 `GameApp.exe`内部断线重连都会重新经过相同决策；固定端点锁不消费登录会话，也不禁止合法重连。

### VMProtect 加固

- 端点解码、共享配置校验、VMP/CRC 门禁、连接分类、固定 `sockaddr_in`构造和 fail-closed 决策拆为小型 `noinline`函数。
- 上述函数全部使用不同名称的 `VMProtectBeginUltra/VMProtectEnd`标记，并加入 `.vmp`工程的保护列表。
- 正式保护工程继续使用 Ultra 编译类型；仅扩大关键决策覆盖，不对日志、模块枚举和无关网络代码做全量虚拟化，避免明显增加启动时间和运行延迟。
- Hook 中不保存可直接替换的明文目标字符串作为权威值；运行时在 Ultra 区域构造目标字节。
- `Debug`、`Release`和`ReleasePlain`用于本地调试/使用，不启用服务器锁或 VMP/CRC 门禁；正式登录器必须通过制品哈希门禁拒绝这些配置产生的 DLL及任何重新编译的 DLL。

## 登录器设计

### `LockedEndpoint`职责

`native/NativeBridge/LockedEndpoint.h`继续作为登录器侧唯一可编辑端点定义。Electron 展示值、NativeBridge 登录连接和 `SpeedHookCfg`均必须与该值一致。

Hook 拥有独立编译进去的固定值以形成第二道校验；自动化测试负责证明两边值一致。Electron 打包过程不得写入或重新生成 Hook DLL。

### 可信制品清单

`resources/native/runtime-artifacts.json`升级为固定运行时制品清单，并作为普通文件收进 `app.asar`。Electron 的 ASAR integrity fuse 保护清单本身。

清单固定记录：

- `QQSpeedNativeBridge.dll`文件名、大小和 SHA-256。
- `qqspeed_clean_hook_64.dll`文件名、大小和 SHA-256。
- `DirSvrInfo1.ini`文件名、大小和 SHA-256。

启动时必须先从受 ASAR 完整性保护的清单读取期望值，再校验 `extraResources/Native`中的源文件。不得根据待校验 DLL 本身动态生成“期望哈希”。校验通过后才允许复制到版本化 LocalAppData 运行目录；复制后和每次复用前再次校验。

### 打包门禁

- `prepack`、`predist`、`predist:portable`和`predist:all`保持只读校验。
- 校验脚本不得调用 MSBuild、VMProtect 或复制 Hook。
- 校验脚本只接受清单中固定的大小和 SHA-256。
- `extraResources`只从 `resources/native/qqspeed_clean_hook_64.dll`取正式 VMP 产物。
- 构建 Hook、执行 VMProtect、把新产物显式放入登录器资源目录、更新清单是独立发布步骤；Electron 打包不得自动从任何旧目录取 DLL。

## 构建顺序

1. 运行 Hook 静态测试并确认新增测试先失败。
2. 修改 Hook，构建 `ReleaseVmp|x64`未保护输入 DLL。
3. 使用现有 `.vmp`工程生成新的 `qqspeed_clean_hook_64.vmp.dll`。
4. 验证保护输出为 x64 DLL，且大小、时间戳和 SHA-256不同于未保护输入。
5. 显式将该保护输出写入登录器的 `resources/native/qqspeed_clean_hook_64.dll`。
6. 构建最新 `QQSpeedNativeBridge.dll`并放入同一资源目录。
7. 更新固定制品清单中的三个文件大小与 SHA-256。
8. 运行所有 Hook、NativeBridge、Electron 和打包门禁测试。
9. 打包 x64 Portable 单文件 EXE。
10. 检查打包前后源 Hook 的大小、修改时间和 SHA-256完全不变，并验证便携包内运行时校验链。

## 测试与验收

### Hook 静态/单元门禁

- 官服源白名单包含三个确认地址。
- 固定目标为 `43.226.61.100:29999`。
- `SpeedHookCfg`给出其他 Host 或 Port 时锁定失败。
- 候选连接校验失败时返回 `WSAEACCES`，不得调用原始目标。
- 非候选连接保持直通。
- `ReleaseVmp`定义 `QQSPEED_VMP_PROTECTION`并链接 `VMProtectSDK64.lib`。
- `.vmp`工程包含所有新增关键 Ultra 标记。

### 登录器门禁

- Electron、NativeBridge 和 Hook 固定目标一致。
- 固定制品清单位于将被打入 ASAR 的路径。
- 修改 Hook、NativeBridge 或 INI 任一字节后，构建前校验和运行时校验均失败。
- 所有打包生命周期脚本只校验、不复制、不构建。
- 旧 Hook 目录名称不存在于打包配置和校验脚本中。

### 集成验证

- 登录器能够正常登录并启动 `GameApp.exe`。
- 首次连接定向到 `43.226.61.100:29999`。
- 关闭一次游戏 Socket 后，`GameApp.exe`内部重连仍定向到同一目标。
- 篡改共享映射目标时，候选连接被拒绝且不会回落官服。
- 替换正式 Hook DLL 时，登录器在注入前拒绝启动游戏。
- 点击开始游戏后的加载动画、最小化和已有密码保存逻辑不回归。

## 性能与错误处理

- VMP 虚拟化只覆盖连接判定的小型函数，不虚拟化整个网络模块。
- 固定目标在 Hook 初始化时校验并缓存；每次重连只读取锁状态并构造固定地址。
- 失败日志不得输出可用于绕过的内部校验细节；仅记录稳定错误码和阶段名称。
- 登录器向用户显示“启动组件校验失败”或“服务器端点校验失败”，不显示期望哈希、内部标记或保护状态细节。

## 回滚

发布前保留现有已验证 Portable EXE及其 Hook/NativeBridge 哈希作为外部发布记录。若新版本无法完成真实启动或重连验证，则不覆盖该已验证发布产物，并停止分发新版本。
