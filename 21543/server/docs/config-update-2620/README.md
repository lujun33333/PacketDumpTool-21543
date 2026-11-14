# packet_dumps_2620 皮肤和新地图配置

补充交付：宠物五表和实际 PK 发包配置见 [pets/README.md](pets/README.md)。

来源：`F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_2620`。
对照配置：`F:\PacketDumpTool\21543\server\Server\bin\Config`。
本目录为离线提取结果，没有修改服务端源码、运行配置或数据库，没有构建、部署或重启服务。

## 可用于更新的地图数据

| 地图 ID | 名称 | 活动 EntryID（不能作为地图 ID） |
| --- | --- | --- |
| 474 | 猫梦浮屋 | 486 |
| 477 | 八闽山海 | 493 |

- `new-map-ids.yml`：两张新增地图的 ID、名称清单。当前 `Map.yml` 不包含这两个 ID。
- `AMapConfig.NewMaps.fragment.yml`：可合并到 `Server/bin/Config/AMapConfig.yml` 的 `NewMaps` 节点。不要用片段覆盖整个文件。该片段只配置新图提示，不负责开赛参数。
- `map-evidence.json`：3 份 msg822 的逐条记录、文件 SHA-256、偏移和原始十六进制。
- `existing-map-catalog.yml`：现有 Map.yml 的 335 条 ID/名称，供检索，不代表本批抓包下发的完整地图表。
- `captured-map-ids.yml`：赛道挑战响应中明确出现的 6 张地图 ID，名称取自本地 Map.yml，均已存在。

地图依据是 msg822 的 `NewMapNum + NewMapTipsCfg[]`，每条结构为大端 `u16 RecordLen + u32 MapID + u16 WordLen + GBK WordTxt`。源码对应 `GameServer.cpp:18062`；读取配置对应 `Main.cpp:876`、`Main.cpp:894`。

猫梦浮屋的记录起始字节是 `00 2b 00 00 01 da 00 23`，地图 ID `0x01DA = 474`；八闽山海是 `00 2b 00 00 01 dd 00 23`，地图 ID `0x01DD = 477`。两者提示均注明 3 星新赛道。活动 EntryID 来自 payload 序号 1631 的福利中心 `ActivityCfg` 响应。

**未生成可直接覆盖的 Map.yml。** 当前 `Map.cpp:23` 的加载器还要求 `Head/End/Loop/Round/Limit`。本次证据没有确认这两张地图的完整开赛参数，不能把 `new-map-ids.yml` 直接当作运行配置，也不能根据相邻 ID 或旧图限时猜填。补齐这些参数需要对应地图的开赛/比赛抓包或客户端地图配置。

## 皮肤结果

| 配置 | 抓包条目 | 本地条目 | 新增 | 已有条目变化 |
| --- | ---: | ---: | ---: | ---: |
| ShapeRefitCfg.yml | 186 | 189 | 0 | 0 |
| LegendarySkin.yml | 13 | 13 | 0 | 0 |
| ShapeColorCfg.yml | 14 组 | 14 组 | 0 | 0 |

本批皮肤分包为 `317 + 714 + 714 + 714`，普通皮肤数量 `50/50/50/36`，传奇配置 `5/5/3/0`，HasMoreInfo 为 `1/1/1/0`。匹配键为 `(KartID, ShapeSuitID)`，基础赛车和皮肤 ID 分开保存。

- `captured-ShapeRefitCfg.yml`、`captured-LegendarySkin.yml`、`captured-ShapeColorCfg.yml`：按当前服务器字段结构提取的抓包配置。
- `added-*.yml`：新增部分；本次均为空。
- `merged-additions-*.yml`：保留本地配置并追加新增项的候选文件；本次与原配置内容相同，无需替换。
- `skin-evidence.json`：完整比对和分包证据。

本地额外的 3 条是：`116734 → 171073 天蓬元帅`、`115160 → 171074 卷帘大将`、`106674 → 171073 齐天大圣`。抓包中未出现不代表应该删除；不要用 captured 全量覆盖掉自定义条目。

msg317 尾部还包含 Origin、Buffer 和当前这三个 YAML 加载器不消费的扩展数据。本目录提取的是当前皮肤配置 schema，不声称恢复了该消息全部业务语义；未解析扩展保留在 `skin-evidence.json`。

## 编码与验证

皮肤 YAML 和地图 ID 清单采用 GB18030（本次文本兼容 GBK），与现有皮肤配置一致；`AMapConfig.NewMaps.fragment.yml` 跟随现有 AMapConfig 的 UTF-8 编码。报告和 JSON 使用 UTF-8。服务端皮肤写包函数直接复制字符串字节，不要随意转换皮肤文件编码。

实际完成：

- 4 个皮肤分包及记录边界、续包结束标记检查。
- 从导出 YAML 重新编码 213 条记录，36,119 字节与原始抓包一致。
- 186 条截断普通皮肤记录全部被拒绝。
- 所有导出 YAML 读取通过；3 份 merged 候选配置与原配置语义相同。
- 两张新地图的数组数量、连续记录长度、WordLen 与 3 份消息交叉比对通过。
- 对提取脚本执行 AST/危险 API 静态扫描并人工检查读写路径，输出限于本目录；没有网络访问、命令执行或不安全反序列化。

这些是离线解析与静态审查结果，未进行 C++ 构建、客户端显示或新图开赛验证。校验摘要与原配置 SHA-256 见 `verification.json`。

复现提取（需要 Python 与 PyYAML，仅写本目录）：

```powershell
python -B F:\PacketDumpTool\21543\server\docs\config-update-2620\extract_configs.py
```
