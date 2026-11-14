# 官服等价多开与加载进度设计

## 已确认的官服行为

- 官服主界面在区服选择与“开始游戏”之间显示独立的“多开”入口，并显示运行实例数量。
- “多开管理”弹层为 720×750 逻辑像素；主账号固定置顶，下面最多绑定 10 个多开账号。
- 同一账号与同一区服不能重复启动；同一账号可以启动不同区服；不同账号可以启动同一区服。
- 全局同一时间只允许一个实例处于 `Starting`，但已进入 `Running` 的实例不阻塞下一个实例启动。
- 实例由账号与区服二元组标识，启动失败或进程退出后才能重试。
- 官服加载图来自 `Releasephysx27_64\LoadingGameUI`。300×200 是逻辑尺寸，在 125% DPI 下显示约 375×250。
- `loader.txt` 证明 `SetLoaderProgressPos(pos, m_nProgressAuto)` 中 `pos` 是阶段码；界面百分比使用真实初始化回调累计值 `m_nProgressAuto`，不是时间插值。

## 本地等价实现

### 实例模型

新增纯状态域 `GameInstanceManager`：

- key：`[account, zone]` 的无歧义序列化结果；
- 状态：`starting -> running -> exited`，错误路径为 `starting -> failed`；
- `startingKey` 是全局启动互斥锁；
- 每个真实启动创建独立 Electron `utilityProcess`，从而拥有独立 Bridge 映像、共享内存名称和 Hook 远程映射；
- 每个 GameApp PID 使用独立进程监视器，退出只回收对应实例。

### 凭据边界

- renderer 只提交一次性表单值，不读取 DPAPI 密文或保存的明文密码；
- 已绑定账号启动时，独立 utility process 按账号从 DPAPI 凭据仓库解密；
- 绑定新账号必须先经过私服真实认证，再写入 DPAPI；失败不生成可启动账号；
- 主账号与绑定账号共用现有仓库格式，但多开列表最多暴露 10 个非主账号。

### UI 与 IPC

- 主界面新增 `多开` 按钮、手柄图标与运行数量；
- 弹层展示主账号、绑定账号、各账号已运行区服、区服下拉和真实“启动”按钮；
- IPC：获取快照、绑定账号、启动实例、解绑账号；
- 主进程广播实例快照，UI 不自行伪造运行状态。

### 加载窗口

- 只读取 `Releasephysx27_64\LoadingGameUI\LoaderBG.bmp`、`Progress_Back.bmp`、`Progress_Fore.bmp`、`cancel_normal.bmp`；
- 根据窗口 DPI 缩放 300×200 逻辑画布；
- 删除 elapsed-time 百分比和完成前的 sleep ramp；
- 只有客户端权威阶段事件推进回调计数，客户端就绪才强制 100%。

## 验证门禁

1. 单元测试覆盖重复账号/区服、单 Starting、多 Running、退出回收、10 个绑定账号上限。
2. 源码边界测试拒绝旧 `Loader` 路径、硬编码里程碑和基于时间的进度插值。
3. renderer 集成测试要求多开按钮、弹层、绑定表单与 IPC 均存在。
4. 真实验证记录两个不同账号的 GameApp PID；关闭其中一个时另一个仍保持 Running。
5. 安装包验证仍需满足 Bridge/Hook 不以独立 DLL 落盘。
