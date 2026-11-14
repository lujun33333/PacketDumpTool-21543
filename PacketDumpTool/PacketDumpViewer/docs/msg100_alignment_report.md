# MsgID 100 全量对齐报告

## 输入证据

- payload 根目录：`F:\PacketDumpTool\PacketDumpTool\x64\Release`
- 最新字段名来源：`F:\new1\dll\new\21273\Network.dll`
- offset/value 来源：`packet_dumps_*` 中的 MsgID 100 payload
- full 标准：`317329 bytes`；body 标准：`317315 bytes = full[0x0E:]`

## 样本分类

| category | count |
|---|---:|
| `before_inner_encoded_or_safe_layout` | 22 |
| `cache_after_full_view` | 44 |
| `cache_before_not_raw_layout` | 44 |
| `client_request_serialize_body` | 22 |
| `trusted_body_full_minus_0x0e` | 44 |
| `trusted_full` | 44 |

## 长度分布

| declared length | count |
|---:|---:|
| `15725` | 1 |
| `15954` | 1 |
| `16133` | 1 |
| `1663` | 1 |
| `1680` | 1 |
| `1682` | 1 |
| `1687` | 1 |
| `1689` | 1 |
| `1692` | 1 |
| `1694` | 1 |
| `1695` | 2 |
| `1696` | 1 |
| `1697` | 1 |
| `1698` | 2 |
| `1701` | 3 |
| `1703` | 1 |
| `1705` | 1 |
| `1708` | 1 |
| `1709` | 1 |
| `1711` | 1 |
| `1729` | 1 |
| `19450` | 1 |
| `19563` | 1 |
| `19601` | 1 |
| `19647` | 1 |
| `19652` | 1 |
| `19733` | 1 |
| `19752` | 1 |
| `19769` | 1 |
| `19810` | 1 |
| `19826` | 1 |
| `19835` | 1 |
| `19862` | 1 |
| `19895` | 1 |
| `19918` | 1 |
| `19999` | 1 |
| `2632` | 1 |
| `2887` | 1 |
| `317315` | 44 |
| `317329` | 132 |
| `4236` | 1 |
| `4307` | 1 |

## dump 目录分布

| folder | count |
|---|---:|
| `packet_dumps_12572` | 20 |
| `packet_dumps_13988` | 20 |
| `packet_dumps_18096` | 30 |
| `packet_dumps_24036` | 30 |
| `packet_dumps_25944` | 30 |
| `packet_dumps_28908` | 20 |
| `packet_dumps_34652` | 20 |
| `packet_dumps_4124` | 20 |
| `packet_dumps_8068` | 30 |

## 21273 Network.dll 字段名证据

| group | count | examples |
|---|---:|---|
| `response_login_class` | 1 | `CSSafeCResponseLoginPara` |
| `task` | 8 | `m_astPlayerDBTaskInfo[%d].m_iTaskID:%d`<br>`m_astPlayerDBTaskInfo[%d].m_shOptionID:%d`<br>`m_astPlayerDBTaskInfo[%d].m_cStatus:%d`<br>`m_astPlayerDBTaskInfo[%d].m_bPrevStatus:%u`<br>`m_astPlayerDBTaskInfo[%d].m_iProgressRate:%d`<br>`m_astPlayerDBTaskInfo[%d].m_iOther:%d`<br>`m_astPlayerDBTaskInfo[%d].m_uiTakeTime:%u`<br>`m_astPlayerDBTaskInfo[%d].m_uiLastUpdateTime:%u` |
| `item` | 7 | `m_astPlayerDBItemInfo[%d].m_iItemID:%d`<br>`m_astPlayerDBItemInfo[%d].m_iItemNum:%d`<br>`m_astPlayerDBItemInfo[%d].m_iAvailPeriod:%d`<br>`m_astPlayerDBItemInfo[%d].m_bStatus:%u`<br>`m_astPlayerDBItemInfo[%d].m_unObtainTime:%u`<br>`m_astPlayerDBItemInfo[%d].m_iOtherAttribute:%d`<br>`m_astPlayerDBItemInfo[%d].m_ushItemType:%u` |
| `base` | 61 | `m_stPlayerDBBaseInfo.m_szNickname[%d]:`<br>`m_stPlayerDBBaseInfo.m_bGender:%u`<br>`m_stPlayerDBBaseInfo.m_bCountry:%u`<br>`m_stPlayerDBBaseInfo.m_iLicense:%d`<br>`m_stPlayerDBBaseInfo.m_unExperience:%u`<br>`m_stPlayerDBBaseInfo.m_iSuperMoney:%d`<br>`m_stPlayerDBBaseInfo.m_iMoney:%d`<br>`m_stPlayerDBBaseInfo.m_unWinNum:%u`<br>`m_stPlayerDBBaseInfo.m_uiSecondNum:%u`<br>`m_stPlayerDBBaseInfo.m_uiThirdNum:%u`<br>`m_stPlayerDBBaseInfo.m_unTotalNum:%u`<br>`m_stPlayerDBBaseInfo.m_iCurHonor:%d`<br>`m_stPlayerDBBaseInfo.m_uiTotalHonor:%u`<br>`m_stPlayerDBBaseInfo.m_iTodayHonor:%d`<br>`m_stPlayerDBBaseInfo.m_iRelaxTime:%d`<br>`m_stPlayerDBBaseInfo.m_uiMonthDurationBefore:%u`<br>... +45 |
| `relation` | 11 | `m_stPlayerRelationInfo.m_uiSrcUin:%u`<br>`m_stPlayerRelationInfo.m_iRelationFlag:%d`<br>`m_stPlayerRelationInfo.m_uiRelationUin:%u`<br>`m_stPlayerRelationInfo.m_szRelationNickname[%d]:`<br>`m_stPlayerRelationInfo.m_u32EngageTime:%u`<br>`m_stPlayerRelationInfo.m_uiNextCanBookingTimeLimit:%u`<br>`m_stPlayerRelationInfo.m_uiBeginHoneyMoonTime:%u`<br>`m_stPlayerRelationInfo.m_uiEndHoneyMoonTime:%u`<br>`m_stPlayerRelationInfo.m_bEngageFlag:%u`<br>`m_stPlayerRelationInfo.m_uSrcRoleID:%llu`<br>`m_stPlayerRelationInfo.m_uRelationRoleID:%llu` |
| `record` | 13 | `m_astRecord[%d].m_u8Type:%u`<br>`m_astRecord[%d].m_u8Level:%u`<br>`m_astRecord[%d].m_u8Status:%u`<br>`m_astRecord[%d].m_u8Difficulty:%u`<br>`m_astRecord[%d].m_i32MapId:%d`<br>`m_astRecord[%d].m_u32Value:%u`<br>`m_astRecord[%d].m_u32Record:%u`<br>`m_astRecord[%d].m_u32Uin:%u`<br>`m_astRecord[%d].m_u8WorldId:%u`<br>`m_astRecord[%d].m_u8NameLength:%u`<br>`m_astRecord[T2].m_szNickname:`<br>`m_astRecord[%d].m_u8UrlLength:%u`<br>`m_astRecord[T2].m_szRecordUrl:` |
| `pet` | 32 | `m_astPetInfo[%d].m_uiUin:%u`<br>`m_astPetInfo[%d].m_iPetId:%d`<br>`m_astPetInfo[%d].m_szPetName[%d]:`<br>`m_astPetInfo[%d].m_cIsProper:%d`<br>`m_astPetInfo[%d].m_usLevel:%u`<br>`m_astPetInfo[%d].m_ucStatus:%u`<br>`m_astPetInfo[%d].m_usPetSkillNum:%u`<br>`m_astPetInfo[%d].m_astPetSkillList[%d].m_usSkillID:%u`<br>`m_astPetInfo[%d].m_astPetSkillList[%d].m_ucStatus:%u`<br>`m_astPetInfo[%d].m_astPetSkillList[%d].m_cActive:%d`<br>`m_astPetInfo[%d].m_astPetSkillList[%d].m_iValue:%d`<br>`m_astPetInfo[%d].m_iStrengLevel:%d`<br>`m_astPetInfo[%d].m_ucTopStatus:%u`<br>`m_astPetInfo[%d].stCommonInfo.m_iHuanHuaLevel:%d`<br>`m_astPetInfo[%d].stCommonInfo.m_uchCanHuanHua:%u`<br>`m_astPetInfo[%d].stCommonInfo.m_ucCanDrive:%u`<br>... +16 |
| `overdue` | 3 | `m_astOverdueItem[%d].m_iItemID:%d`<br>`m_astOverdueItem[%d].m_iOverdueTime:%d`<br>`m_astOverdueItem[%d].m_iOtherAttr:%d` |
| `noble` | 4 | `m_stNobleInfo.m_u32NobleID:%u`<br>`m_stNobleInfo.m_u8NobleLevel:%u`<br>`m_stNobleInfo.m_u32NoblePoint:%u`<br>`m_stNobleInfo.m_u32NobleLeftDays:%u` |
| `guild_vip` | 2 | `m_stGuildVipBaseInfo.m_u8GuildVipLevel:%u`<br>`m_stGuildVipBaseInfo.m_u32GuildVipPoint:%u` |
| `ranked` | 10 | `m_stRankedMatchGradeInfo[%d].m_uiGradeLevel:%u`<br>`m_stRankedMatchGradeInfo[%d].m_uiChildLevel:%u`<br>`m_stRankedMatchGradeInfo[%d].m_uiGradeScore:%u`<br>`m_stRankedMatchGradeInfo[%d].m_uiMaxGradeScore:%u`<br>`m_stRankedMatchGradeInfo[%d].m_uiTotalScore:%u`<br>`m_stRankedMatchGradeInfo.m_uiGradeLevel:%u`<br>`m_stRankedMatchGradeInfo.m_uiChildLevel:%u`<br>`m_stRankedMatchGradeInfo.m_uiGradeScore:%u`<br>`m_stRankedMatchGradeInfo.m_uiMaxGradeScore:%u`<br>`m_stRankedMatchGradeInfo.m_uiTotalScore:%u` |

## trusted full 对齐结论

- trusted full 样本数：`44`
- unique `(uin,item,map,pet,extend)` 组合：`5`
- layout_ok 分布：`{1: 44}`
- item_num 分布：`{88: 4, 62: 2, 47: 2, 200: 36}`
- map_num 分布：`{6: 4, 1: 4, 298: 30, 225: 6}`
- pet_num 分布：`{1: 8, 50: 30, 32: 6}`
- extend_item_num 分布：`{1600: 8, 2750: 30, 1900: 6}`

## body/full 对齐结论

- body 样本数：`44`
- body == full[0x0E:] 命中：`44`
- 未命中：`0`

## cache_after 对齐结论

- cache_after full-view 样本数：`44`
- unique `(uin,item,map,pet,extend)` 组合：`7`
- layout_ok 分布：`{1: 44}`

## before_inner / TaskBlock 对齐结论

- before_inner 样本数：`22`
- loginpara decoder OK：`22`
- cursor 完整消费：`1`
- `obj+0x310328` / `varint_0x310328` task count 分布：`{'0': 19, 'None': 3}`
- `obj+0x310330` / `subobj_array_0x310330` task array count 分布：`{'0': 19, 'None': 3}`
- 当前可解码 before_inner 是否全部 taskNum=0：`True`

结论：`CSSafeSTPlayerDBTaskInfo` 字段名、对象位置和记录写入顺序已经由 21273 `Network.dll`/IDA export 证明；但当前 `packet_dumps_*` 中所有可解码 before_inner 的 `m_shTaskNum` 都是 0，没有任何 `m_astPlayerDBTaskInfo[i]` 实例字节可对齐到 317329 full 展开区。因此 v1.1/v1.2 不能伪造 full offset，只能把该块标为 `Unaligned / no non-zero task sample`。

### CSSafeSTPlayerDBTaskInfo 记录顺序证据

- ResponseLogin 对象证据：`obj+0x310328` 是 task count，`obj+0x310330` 是 task array；safe-object stride=`320`，capacity=`1100`。
- 记录字段顺序证据：`sub_1800D2CB0` / `CNotifyAddTaskInfoPara` 按 24-byte record 打印 8 个 `m_astPlayerDBTaskInfo[i].*` 字段。

| record offset | size | type | field |
|---:|---:|---|---|
| `+0x00` | 4 | `le:i32` | `m_astPlayerDBTaskInfo[i].m_iTaskID` |
| `+0x04` | 2 | `le:i16` | `m_astPlayerDBTaskInfo[i].m_shOptionID` |
| `+0x06` | 1 | `i8` | `m_astPlayerDBTaskInfo[i].m_cStatus` |
| `+0x07` | 1 | `u8` | `m_astPlayerDBTaskInfo[i].m_bPrevStatus` |
| `+0x08` | 4 | `le:i32` | `m_astPlayerDBTaskInfo[i].m_iProgressRate` |
| `+0x0C` | 4 | `le:i32` | `m_astPlayerDBTaskInfo[i].m_iOther` |
| `+0x10` | 4 | `le:u32` | `m_astPlayerDBTaskInfo[i].m_uiTakeTime` |
| `+0x14` | 4 | `le:u32` | `m_astPlayerDBTaskInfo[i].m_uiLastUpdateTime` |

### IDA xref 摘要

| ref rva | function | function rva | string |
|---:|---|---:|---|
| `0xD2BA4` | `sub_1800D2B60` | `0xD2B60` | `m_stPlayerDBLicenseInfo.m_shTaskNum:%d\n` |
| `0xD2CC7` | `sub_1800D2CB0` | `0xD2CB0` | `[%s]--------------------CNotifyAddTaskInfoPara--------------------\n` |
| `0xD2CE5` | `sub_1800D2CB0` | `0xD2CB0` | `m_shTaskNum:%d\n` |
| `0xD2D0A` | `sub_1800D2CB0` | `0xD2CB0` | `m_astPlayerDBTaskInfo[%d].m_iTaskID:%d\n` |
| `0xD2D1C` | `sub_1800D2CB0` | `0xD2CB0` | `m_astPlayerDBTaskInfo[%d].m_shOptionID:%d\n` |
| `0xD2D2F` | `sub_1800D2CB0` | `0xD2CB0` | `m_astPlayerDBTaskInfo[%d].m_cStatus:%d\n` |
| `0xD2D42` | `sub_1800D2CB0` | `0xD2CB0` | `m_astPlayerDBTaskInfo[%d].m_bPrevStatus:%u\n` |
| `0xD2D54` | `sub_1800D2CB0` | `0xD2CB0` | `m_astPlayerDBTaskInfo[%d].m_iProgressRate:%d\n` |
| `0xD2D66` | `sub_1800D2CB0` | `0xD2CB0` | `m_astPlayerDBTaskInfo[%d].m_iOther:%d\n` |
| `0xD2D78` | `sub_1800D2CB0` | `0xD2CB0` | `m_astPlayerDBTaskInfo[%d].m_uiTakeTime:%u\n` |
| `0xD2D8A` | `sub_1800D2CB0` | `0xD2CB0` | `m_astPlayerDBTaskInfo[%d].m_uiLastUpdateTime:%u\n` |
| `0xF0ED7` | `sub_1800F0E50` | `0xF0E50` | `m_astPlayerDBLicenseInfo[%d].m_shTaskNum:%d\n` |
| `0xF2545` | `sub_1800F20D0` | `0xF20D0` | `m_astSystemTaskPackInfo[%d].m_shTaskNum:%d\n` |
| `0xF263A` | `sub_1800F25F0` | `0xF25F0` | `m_shTaskNum:%d\n` |
| `0x138965` | `sub_1801388A0` | `0x1388A0` | `m_astSystemTaskPackInfo[%d].m_shTaskNum:%d\n` |
| `0x20B641` | `sub_1801F88E0` | `0x1F88E0` | `CNotifyAddTaskInfoPara` |

## 不能直接按 full offset 对齐的类别

- `cache_before_not_raw_layout`：过滤/缓存前视图，数量字段会出现不可信值，不能作为官服 raw layout。
- `before_inner_encoded_or_safe_layout`：长度约 2.6KB-20KB，内容是 CSSafe wire/object 形态；可用 `qqspeed_decoder.loginpara` 做对象字段证据，但不等同 317329 full 展开 offset。
- `client_request_serialize_body`：客户端登录请求，不是 `CSSafeCResponseLoginPara` 响应结构。

## 官服 count=0 的填法

- 数量字段为 0 时，只填 count 本身：例如 `m_shTaskNum = 0`。
- 对应数组显示为空：例如 `m_astPlayerDBTaskInfo[] = empty`。
- 不生成 `m_astPlayerDBTaskInfo[0].m_iTaskID = 0`，因为这会把“没有第 0 条记录”误写成“存在一条全 0 记录”。
- 对 `m_astPlayerDBItemInfo[]` / `m_astRecord[]` / `m_astPetInfo[]` 这种已经由 full 长度证明存在固定展开槽位的区域，count=0 时仍可显示 `ReservedSlots`，但 active record 数为 0。
- 对 `m_astPlayerDBTaskInfo[]` 这种当前只在 before_inner 对象层证明的动态数组，count=0 时只显示 empty + record layout template，不给 full offset 编假记录。

### 已套用 count=0 规则的数组

| array | count field | layout | count=0 display |
|---|---|---|---|
| `m_astPlayerDBItemInfo[]` | `m_iItemNum` | fixed full slots | active_records=0; ReservedSlots/default area remains |
| `m_astRecord[]` | `m_astRecordCount (count name not official)` | fixed full slots | active_records=0; ReservedSlots/default area remains |
| `m_astPetInfo[]` | `m_usPetNum` | fixed full slots | active_records=0; ReservedSlots/default area remains |
| `m_astPetInfo[i].m_astPetSkillList[]` | `m_usPetSkillNum` | fixed 3 slots inside each pet record | active_skills=0; 3 skill slots are inactive/default |
| `m_astOverdueItem[]` | `m_uchOverdueItemNum (count name not official)` | fixed full slots | active_records=0; ReservedSlots/default area remains |
| `m_astPlayerDBTaskInfo[]` | `m_shTaskNum / obj+0x310328` | dynamic before_inner object array; full nonzero offset not proven | empty array only; do not synthesize element[0] |

## 当前仍保留 UnknownName / Unaligned 的 full offset 清单

| range | field/block | status | reason |
|---|---|---|---|
| `0x00033BBE-0x00033D7B` | `m_stPlayerRelationInfo expanded / padding` | `UnknownName` | Network.dll 有 m_stPlayerRelationInfo.* 字段名，但 full 展开区是固定占位；当前 dump 非零 run 太少，不能把每个成员提升成 full offset。 |
| `0x00033D7C-0x00033D8B` | `PreMapUnknownU32_1..4` | `UnknownName` | 反编译顺序显示在 map/record array 前有 4 个 u32；未找到能证明语义的 21273 字符串/消费点。 |
| `0x00033D8C-0x00033D8D` | `m_astRecord count` | `UnknownName` | 后续 m_astRecord[] stride=12 已由多 dump 验证；count 字段自身未找到直接官方成员名。 |
| `m_astPetInfo[i]+0x20/+0x24/+0x28/+0x2D/+0x2F` | `pet level/PL fields` | `UnknownName` | offset/value 规律稳定，但 21273 字符串没有对应直接成员名；保留描述名，不伪造成 m_astPetInfo 官方路径。 |
| `m_astPetInfo[i]+0xBA-0xBE` | `m_astPetInfo[i].CommonTailUnknown` | `UnknownName` | Network.dll 有 stCommonInfo/PKStar/PetSkinID/RoleID 字符串，但当前 5 字节尾部不能同时容纳并证明这些字段。 |
| `0x0004CA2E` | `m_astOverdueItem count` | `UnknownName` | m_astOverdueItem[] 三个成员已确认；count 名称未找到直接字符串。 |
| `0x0004CA43-0x0004CF1F` | `PreFilterHasMoreInfo.RawBlock` | `UnknownName/Reserved` | 该区在 full 中为模板/保留混合区；多 dump 有非零 run，但未找到稳定消费点可命名。 |
| `0x0004D022-0x0004D02D` | `TailU32_1..3` | `UnknownName` | 位于 TssTicketLen 后、FinalFixedArea 前；反编译字段表仍是通用 m_uiPara/tail 形态，未找到官方成员名。 |
| `0x0004D02E-0x0004D790` | `PostExtendTail.FinalFixedArea` | `UnknownName/Reserved` | full 尾部固定 1891 bytes；当前 dump 内存在非零 run，不能整体标 Reserved，也不能无证据拆字段。 |
| `before_inner obj+0x310328 / obj+0x310330` | `m_astPlayerDBTaskInfo[]` | `VerifiedEmpty + Template` | 当前所有可解码 before_inner 的 taskNum=0，官服表达为空数组；字段名/记录顺序/对象 offset 已确认，非零记录的 317329 full offset 仍需 taskNum>0 样本。 |

## 后续落 UI 的规则

1. 字段名只从 `F:\new1\dll\new\21273\Network.dll` 字符串/RTTI/反编译确认。
2. offset 只从 trusted full 和 body/full 对齐确认。
3. `m_astPlayerDBTaskInfo[]` 当前 dumps 的 taskNum 全为 0，按官服表达为 `VerifiedEmpty + Template`；非零记录的 317329 full offset 仍需 taskNum>0 样本。
4. 未定位字段显示 `UnknownName`，但必须带 `name source / offset source / unresolved reason`。
