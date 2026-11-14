# 官服等价登录器重构设计

日期：2026-07-16  
状态：用户已确认，直接实施（用户另有备份）

## 目标

在不复制官服私有模块、且保持现有私服协议兼容的前提下，把登录器重构为官服的可观察架构：Electron 负责配置、页面、登录状态与游戏调度；`qqspeed_clean_hook_64.dll` 和薄 C++ 桥仅保留 Hook 注入、共享内存、命名事件及 Windows 必需能力。

## 证据基线

- 官服 `1.0.0.14` 的 Electron 层负责内容配置、账号元数据、状态和进程编排；密码票据与 GameApp 低层能力由原生模块完成。
- 官服主页来自远程配置并动态组合背景、轮播、公告和版本时间线，不是整页截图。
- 官服持久化 `uin`、记住密码、最后登录时间等元数据；密码实体由原生登录组件处理。当前实现由独立登录 utility process 使用 Windows DPAPI 持有密码实体，main 只提交 metadata-only 账号记录；最终实现与验证结论以 `docs/2026-07-16-official-equivalent-refactor-report.md` 为准。
- 私服客户端启动仍依赖 `KartMap_Shared_<pid>`、`RunSpeedGameEvent_<pid>` 与 Hook 注入；这些属于 Windows/客户端 ABI，不能纯网页化。
- 当前私服路由由锁定端点控制，`DirSvrInfo1.ini` 不参与活动登录路由，是可移除的打包残留。

## 边界

### Electron 主进程

- 创建安全 BrowserWindow、校验 IPC sender。
- 启动独立 utility process；不在 renderer 或主进程执行私服密码协议。
- 管理凭据保险库、设置、内容配置缓存和受控外链。
- 只把最小渲染 DTO 发给 renderer，不暴露密钥或原始本地路径。

### JS utility process

- 唯一登录状态机：`idle -> preparing -> waitingCredentials -> authenticating -> authenticated -> starting -> running`，失败/取消进入终态。
- 实现私服 TCP framing、QQ TEA、登录请求/响应解析、超时和取消。
- 登录成功后把 `uin/gameKey/signKey` 一次性传给薄原生桥；日志永不打印密码、MD5、GameKey、SignKey。
- 调用薄原生桥开始 Hook 会话、提交认证材料、触发开始事件和取消。

### 薄 C++ 桥

- 保留共享内存/命名事件、GameApp 创建与监视、`LoadLibrary` Hook 注入及 Win32 错误转换。
- 不再联网、不再解析私服登录包、不再保存密码、不再拥有业务状态机。
- 删除 `PrivateLogin.cpp`、`QqTea.c`、INI 路由和旧登录 UI 依赖；导出语义改为 `BeginSession/ApplyAuth/StartGame/Cancel/GetState`。

### Renderer

- 登录页左区支持图片或视频；视频可静音/暂停。
- 支持背景媒体音轨及独立音乐源，统一音量/静音状态持久化。
- 主页根据规范化配置动态渲染背景、轮播、公告、版本时间线和区服选择。
- 删除整页截图伪装、假商城数据和不可用入口；不可用功能明确降级而不是做死数据。

## 凭据模型

1. 用户输入仅进入 preload 的窄 IPC 接口。
2. utility process 使用短生命周期凭据认证；所有结束路径清空引用。
3. 只有收到服务端成功响应并完成 native commit 后，utility 才写 DPAPI vault，main 再原子更新账号元数据；任一阶段失败都必须回滚或明确降级为不记住密码。
4. 未勾选记住密码时只保留账号元数据，删除对应密码密文。
5. 自动登录只能使用已成功认证保存的密文；解密失败立即降级为手动输入。

## 动态内容

- 首选官服配置接口并设置严格超时、大小限制、schema 白名单与本地缓存。
- 网络失败使用最后一次有效缓存；首次离线使用内置结构化 fallback，而不是整页截图。
- 远程 URL 只允许 `https` 和明确 CDN/官网 host；HTML 文本使用 DOM 文本节点渲染。
- 视频加载失败自动回退背景图，音乐缺失不阻塞登录和启动。

## 打包与清理

- `DirSvrInfo1.ini` 从 `extraResources`、runtime manifest、stage/check 脚本和资源目录全部移除。
- `home-bg-official-qqspeed.png` 不再被生产代码引用，并从交付资源删除。
- 构建产物目录保留为本地生成物但不作为源码真相；清理 C++/Rust 的可再生中间产物只在确认打包不引用后执行。

## 验证标准

- 协议 golden tests 与边界/错误/取消测试通过。
- C++ Release x64 构建通过，产物只含新的最小导出与 Hook 运行依赖。
- 所有现有安全/IPC/生命周期检查继续通过。
- 独立 user-data 目录完成一次页面启动、动态内容渲染和媒体控件 smoke。
- 私服账号完成登录到 `authenticated`，随后由用户明确动作触发开始游戏；测试日志不含秘密。
- 官服登录仅做无输入/无提交的可观察验证；没有完整凭据时不声称官服认证成功。

## 已知非等价点

- 官服使用腾讯闭源 `login.node/ssologin.exe/kernel.node/rpckit.node`，私服不能直接复用；本项目以 JS 私服协议 + 薄 Win32 桥实现相同职责分层。
- 官服内容服务随线上配置变化；离线 fallback 只能保证结构与交互等价，不能保证活动素材实时相同。
- 继续保留更严格的 sandbox、context isolation 和 IPC 校验，不复制官服观察到的弱安全设置。
