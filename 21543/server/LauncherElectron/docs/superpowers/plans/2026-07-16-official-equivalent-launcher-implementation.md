# 官服等价登录器实施计划

> 按已确认设计直接实施。仓库目录当前没有 Git 元数据，因此用逐文件可见 diff、测试日志和最终文件清单代替提交点。

## 1. 建立测试入口与协议契约

- 在 `test/unit` 增加 Node test runner 用例。
- 先写 QQ TEA、封包/解包、响应解析、超时/取消的失败测试并确认 RED。
- 在 `package.json` 增加 `test:unit` 与统一 `test`，不破坏现有 check scripts。

## 2. 把私服登录迁入 utility process

- 新建 `src/main/login/qq-tea.js`、`private-protocol.js`、`private-login-client.js`。
- 新建/重构 utility worker 的显式状态机；主进程只转发状态事件。
- 密码、摘要和会话 key 不进入日志或 renderer DTO。
- 运行协议单测并与既有 C++ 实现生成的固定向量交叉验证。

## 3. 缩小原生桥

- 先写导出/源码边界检查，证明 C++ 仍包含登录协议（RED）。
- 将导出改为开始 Win32/Hook 会话、应用 JS 登录结果、开始游戏、取消和查询状态。
- 从 Visual Studio 工程移除 `PrivateLogin.cpp`、`QqTea.c`、MD5/INI 登录依赖。
- Release x64 构建并运行导出/依赖检查。

## 4. 修复凭据提交语义

- 抽出账号元数据与密码 vault；写“失败不覆盖、成功原子提交、取消记住即删除”的单测。
- 主流程改为认证成功消息之后持久化。
- 自动登录解密失败安全降级，不把错误密文带入认证。

## 5. 动态官服式内容与媒体

- 新建内容 schema/normalizer/cache service；为恶意字段、超大响应、超时和 fallback 写测试。
- renderer 拆分为状态、动态主页、媒体控制模块。
- HTML/CSS 改为真实 DOM 组件；去除整页截图背景和硬编码活动/公告。
- 登录左区支持 image/video，增加视频、音乐、音量、静音持久化和失败回退。

## 6. 移除 DirSvrInfo 与死逻辑

- 删除所有打包、manifest、stage/check 对 `DirSvrInfo1.ini` 的引用并删除资源文件。
- 删除生产引用后的整页截图、假商城数据、无效 preload alias 和不可达 renderer 分支。
- 聚焦搜索确保 `DirSvrInfo1.ini`、旧 C++ 网络登录入口和 screenshot class 零生产引用。

## 7. 集成与验证

- 运行 `npm test`、全部现有 `check:*`、native build/check 与打包 dry run。
- 使用隔离 user-data 启动一次，验证登录页、动态主页、媒体控件、窗口生命周期和无意外外联。
- 使用用户提供的私服测试账号只验证授权私服链路；不在任何命令/日志/报告回显凭据。
- 记录 `authenticated`、共享内存写入、开始事件与 GameApp 生命周期的最小证据。

## 8. 最终审计

- 审查 diff、调用链、打包产物和秘密扫描。
- 列出 changed files、官服一致点、必要不一致点、通过的验证、失败/跳过/无法验证项。
