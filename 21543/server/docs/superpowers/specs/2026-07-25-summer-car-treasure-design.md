# Summer Car Treasure Official-Compatible Design

## 目标与范围

在保留现有老版赛车夺宝的前提下，新增 2026-07 官服“狂欢夺宝”版本。
新版本使用独立 QuickRPC 方法、结构体、YAML 配置和玩家状态；不固定奖品、
不回放整包、不强制出车，也不额外实现抓包之外的保底覆盖。

本期包含：

- 普通夺宝和狂欢夺宝双模式配置。
- 普通模式自选赛车。
- 单抽、十连、扣除夺宝券和动态权重抽奖。
- 赛车暂存、功能道具直接入包。
- 暂存领取和暂存分解。
- 狂欢值、晶石值及相关展示状态持久化。
- 与官服一致的 `24251` QuickRPC 响应字段、`11266` 扣券通知和 `529`
  入包通知。

不修改现有 `Co.RequestGetCarTreasureCfg`、
`Co.RequestGetCarTreasureAward` 和
`Co.Beta80_RequestSelectGroupIndex` 的行为。

## 协议入口

新增三个精确匹配的 QuickRPC 路由：

```text
Co.RequestGetSummerCarTreasureCfg
Co.RequestGetSummerCarTreasureAward
Co.Beta96_RequestSelectGroupIndex
```

配置请求参数：

- 第一个业务参数 `0`：普通模式。
- 第一个业务参数 `1`：狂欢模式。
- 第二个业务参数保留并接受官服已观察的 `0/1/2`，不让它改变奖池规则。

奖励请求操作码：

- `0`：普通奖池抽奖。
- `1`：领取暂存物品。
- `2`：分解暂存物品。
- `3`：狂欢奖池抽奖。

抽奖操作的第二参数仅接受 `1` 或 `10`。非法模式、操作码、次数或格式返回
配置中的错误码，且不得扣券或写入状态。

## 结构体模型

新增独立数据类型，不在处理函数中硬编码物品、概率、期限或活动日期：

```cpp
struct SummerCarTreasureAwardCfg;
struct SummerCarTreasurePoolCfg;
struct SummerCarTreasureRuntimeCfg;
struct SummerCarTreasurePlayerState;
struct SummerCarTreasureDrawResult;
```

`SummerCarTreasureAwardCfg` 保存：

- `ItemID`
- `ItemNum`
- `ItemType`
- `AvailPeriod`
- `Probability`
- `ExchangePieceNum`
- `CanExchange`
- `OtherAttribute`
- `GroupIndex`
- `SelectCarType`
- `BelongSummerPool`

`SummerCarTreasurePoolCfg` 保存模式、单抽成本、十连成本和奖励列表。

`SummerCarTreasureRuntimeCfg` 保存活动期 ID、时间范围、消耗和分解物品 ID、
界面字段、进度上限、错误码、普通池及狂欢池。

`SummerCarTreasurePlayerState` 保存狂欢值、晶石值、晶石领取数和折扣使用数。

`SummerCarTreasureDrawResult` 是一次抽奖的不可变结果，负责连接抽奖计算、
数据库落库、通知和 QuickRPC 响应，避免各阶段重新随机。

## YAML 配置

新增：

```text
Server/bin/24251/SummerCarTreasure.yml
```

配置根节点为 `summer_car_treasure`，包含：

- `enabled`、`activity_id` 和协议方法名。
- 活动起止日期及时间。
- `cost_item_id=99662`、普通成本 `1/10`、狂欢成本 `3/30`。
- `exchange_piece_item_id=94880`。
- 狂欢值上限 `800`、晶石值上限 `450`。
- 官服界面字段及错误码。
- 普通奖池 70 条。
- 狂欢奖池 62 条。

服务端启动或配置重载时解析为结构体。以下情况拒绝加载新配置并保留上一个
有效快照：

- 必填节点缺失。
- 物品 ID、数量、概率或成本非法。
- 奖池为空，或同一模式内出现
  `(ItemID, AvailPeriod, GroupIndex, BelongSummerPool)` 完全相同的重复行；
  同一物品的永久和限时配置是合法的独立奖励。
- 普通模式自选组不足以满足 S/T/A 选择数量。
- 活动日期或进度上限非法。

线上抽奖只读取已验证的不可变配置快照，不在每次请求中重新读取 YAML。

## 奖池与随机规则

普通池：

- 返回官服 70 条配置。
- 无 `GroupIndex` 的固定奖励始终参加抽取。
- 有 `GroupIndex` 的赛车仅在玩家已选组中参加抽取。
- 非赛车功能道具始终参加抽取。

狂欢池：

- 返回官服 62 条配置。
- 每条带 `iBelongSummerAwardPoll=1`。
- 不使用普通模式的自选组过滤。

每一抽对当前有效奖池执行一次整数权重抽样：

```text
hit = uniform_integer(1, sum(positive Probability))
```

按累计权重命中一条奖励。生产代码不使用固定 seed、固定奖励数组、
“十连至少一辆车”或额外保底逻辑。永久奖励和限时奖励是独立配置行。

## 官服首次自选赛车

新活动期首次打开普通夺宝时，如果当前玩家在
`CarTreasureSelectedGroup` 中没有该 `ActivityId` 的完整有效选择：

- 配置响应返回 `iNeedToSelectCar=1`。
- `iAlreadySelectCarGroupIndex` 返回空表。
- `iCanResetSelect=0`。
- 客户端弹出官服自选界面；服务端不预填、不固定任何赛车组。

`Co.Beta96_RequestSelectGroupIndex` 的请求数据是 QuickRPC `FD` 索引表
`{slotIndex -> groupIndex}`。服务端按当前普通奖池结构动态校验：

- 槽位索引必须唯一且连续为 `1..12`。
- 赛车组索引必须唯一，并且存在于当前普通奖池。
- `SelectCarType=1` 的 S 车组必须选 `3` 个。
- `SelectCarType=3` 的 T 车组必须选 `2` 个。
- `SelectCarType=2` 的 A 车组必须选 `7` 个。

抓包中的 `[1,2,3,14,13,21,22,23,24,16,20,15]` 只是该次玩家选择，
不得写成默认值或固定选择。

首次提交校验成功后，在一个事务中保存全部 12 个槽位。响应沿用官服格式：
`Dispatch(context, 0)` 且没有额外数据。之后重新请求普通模式配置时：

- `iNeedToSelectCar=0`。
- `iAlreadySelectCarGroupIndex` 按保存时的槽位顺序返回索引表。
- 首次选择后的 `hasPermanentItemList` 返回空表。

同一 `ActivityId` 已有完整选择时禁止再次提交，因为官服字段
`iCanResetSelect=0`；切换到新 `ActivityId` 后自然进入新的首次选择。
完成首次选择前，普通模式抽奖请求直接失败且不扣券、不改进度、不写奖励。
狂欢模式不使用普通自选组过滤，也不覆盖玩家的普通自选结果。

## 状态与数据库

新增 `SummerCarTreasureState`：

```text
Uin
ActivityId
LuckValue
CrystalProgress
CrystalGotNum
DiscountUsedNum
UpdateTime
PRIMARY KEY (Uin, ActivityId)
```

普通自选组继续复用 `CarTreasureSelectedGroup`，但使用新活动的
`ActivityId` 隔离老版数据。

赛车暂存继续复用官服同类界面的 `RetComItemTable`。抽中的赛车写入暂存；
宝珠、改装道具等可叠加物品直接写入玩家背包。

一次成功抽奖按已观察官服行为更新：

- 晶石值增加实际消耗的夺宝券数量。
- 狂欢模式下，狂欢值额外增加实际抽取次数。
- 成功十连后 `DiscountUsedNum` 增加 `1`；单抽不改变该字段。
- 状态值不参与改变随机结果，不触发强制奖励。

进度字段按配置上限封顶。未观察到的满值奖励不会被猜测实现。

## 抽奖事务与通知顺序

抽奖必须形成一个数据库事务：

1. 锁定并检查夺宝券数量。
2. 生成一次 `SummerCarTreasureDrawResult`。
3. 扣除夺宝券。
4. 赛车写入暂存箱，道具写入背包。
5. 更新玩家活动状态。
6. 提交事务。

任一步失败则回滚，且不发送成功通知。

提交后依官服顺序发送：

1. `11266` 扣券及最新余额。
2. `529` 直接入包物品；重复道具保留逐次累加后的数量语义。
3. `24251 DispatchCoResponse` 抽奖展示结果。

抽奖响应从 `SummerCarTreasureDrawResult` 生成，字段包括：

```text
m_sItemNum
m_iSuperMoney
m_iCoupons
m_astItemInfo[]
```

`m_iCoupons` 是奖励字段，不是当前夺宝券余额。

## 暂存领取与分解

操作码 `1`：

- 校验请求中的暂存记录确实属于当前玩家。
- 从暂存箱原子删除。
- 按物品类型和期限写入背包。
- 发送 `529` 并返回领取结果。

操作码 `2`：

- 校验并原子删除暂存记录。
- 使用奖励配置中的 `ExchangePieceNum` 计算夺宝能量。
- 增加 `94880`，发送物品变更通知并返回分解结果。

重复请求不得重复领取或重复分解。

## 配置响应

配置响应按模式从结构体动态生成，不存储固定二进制包。

两种模式共享活动时间、消耗物品、暂存表、自选状态和玩家进度。普通模式返回
70 条池及自选组；狂欢模式返回 62 条池及 `iBelongSummerAwardPoll=1`。

官服已观察的关键字段包括：

```text
iCostNumOneTime
iCaostNumTenTime
iInnerCostNumOneTime
iInnerCaostNumTenTime
iCurrentLuckValue
iTotalLuckValue
iJingShiProgress
iJingShiTotalLuckyProgress
iJingShiGotNum
iJingShiCanGotTotalNum
iAlreadySelectCarGroupIndex
RetComItemTable
award_poll_cfg
```

布尔字段继续用 QuickRPC `F8/F9`，数值用 `F6 double`，嵌套表用 `FD`，
键字典继续复用现有 `CT_*` 写包工具。

## 兼容、回滚与观测

- 老版 YAML、方法名、奖池和数据库记录保持不变。
- 新版本由 `SummerCarTreasure.yml: enabled` 独立开关。
- 配置加载失败时仅关闭或保留上一份新版本快照，不影响老版夺宝。
- 日志记录活动 ID、模式、操作码、次数、成本、中奖物品、状态前后值和错误码，
  不记录密钥或无关玩家数据。
- 回滚只需关闭新版本开关或移除新路由；新增状态表保留不破坏老版。

## 验证

新增回归测试覆盖：

- 两个配置模式的字段、奖池数量和关键值。
- 首次普通配置返回 `iNeedToSelectCar=1` 和空的已选表。
- `Beta96` 按 3 个 S、2 个 T、7 个 A 动态校验、一次性保存并重新读取。
- 槽位重复、组重复、越界组、车型数量错误和重复提交均零副作用。
- 选择成功后的普通配置返回 `iNeedToSelectCar=0`，并保持玩家选择顺序。
- 普通/狂欢单抽及十连成本。
- 权重抽样只从有效奖池产生奖励，且无固定十连路径。
- 夺宝券不足、非法操作和非法次数零副作用。
- 赛车进入暂存，道具进入背包。
- 暂存领取、分解及重复请求幂等。
- 狂欢值和晶石值持久化。
- 数据库失败时事务回滚。
- `11266 -> 529 -> 24251` 通知顺序。
- 官服十连样本可以被编码为相同字段结构，但测试不要求随机结果固定为该样本。
- 老版三个 QuickRPC 方法的既有回归仍通过。

最终验证包括 MSVC x64 Release 构建、结构化响应解码、配置校验测试和源码审计。
