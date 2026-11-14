# 官服式预启动与托盘实施计划

> 范围以 `../specs/2026-07-18-official-prelogin-and-tray-design.md` 为准。本计划执行期间不修改官服目录和数据库。

## 1. 建立基线与回滚点

- 记录官服目录关键文件哈希。
- 备份本批将修改的客户端、服务端与测试文件。
- 运行现有登录协议、运行材料、原生生命周期和窗口集成测试，记录已有失败。

## 2. 服务端：允许预登录运行材料

- 先增加失败测试：未认证连接可签发 `subject = 0` 的 v2 运行材料。
- 保留测试：已认证旧客户端仍能以实际 UIN 请求材料。
- 修改 `MyRequestRuntimeMaterial()` 按 version 分流：v1 保持认证门禁，v2 不触碰账号数据库和登录返回协议。
- 验证同一连接重复请求仍被拒绝/断开，避免无限重复签发。

## 3. 登录协议客户端：拆分材料请求与账号登录

- 先增加 `requestPreloadRuntimeMaterial()` 的网络顺序测试：连接后首包只发送 MsgID 3/v2，只接受 MsgID 3/v2 响应。
- `loginPrivateServer()` 新登录路径不传 runtime material；其旧 MsgID 1 -> MsgID 3/v1 能力保持兼容。
- 校验 v2 预登录材料 `subject = 0` 和 `QQSRMAT2`；旧路径继续校验账号 UIN 与 `QQSRMAT1`。

## 4. native worker：真实预加载和同 PID 复用

- 先扩展生命周期测试，要求 `start-session` 内完成 `beginSession()` 并返回有效 PID。
- 将解包、映射、Hook 设置、`beginSession()` 移入初始化阶段。
- 登录阶段只执行账号登录与 `applyAuth()`。
- 把凭证错误恢复逻辑改为保留已映射桥接和预加载 GameApp。
- 增加断言：初始化、认证、开始三个阶段 PID 一致且只调用一次 `beginSession()`。

## 5. Electron 托盘生命周期

- 用纯单元测试实现独立 tray controller：创建幂等、隐藏、恢复、退出回调、销毁。
- 主进程接入：减号普通最小化，关闭按钮隐藏到托盘，托盘退出走完整清理。
- 开始游戏后隐藏到托盘，失败时恢复窗口。

## 6. 构建与验收

- 运行所有 Node 单元/集成测试和 renderer 冒烟测试。
- 构建 NativeBridge 与服务端相关测试配置。
- 运行最小手工 Electron 验证；若当前环境不能连接实际私服，则明确标记同 PID 端到端为待上线前验证。
- 对比官服目录哈希，确认零修改。
- 汇总 changed files、服务端/客户端发布顺序、回滚方法及未验证项。
