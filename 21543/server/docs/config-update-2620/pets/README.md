# 宠物配置更新候选：packet_dumps_2620

已生成与当前源码匹配的五份宠物 YAML，以及独立的 PK 发包配置。这里只写交付目录，未替换 `Server/bin` 的文件，未编译、部署、重启或修改数据库。

## 更新内容

| 文件 | 现有数量 | 候选数量 | 变化 |
| --- | ---: | ---: | --- |
| PetBase.yml | 553 | 555 | 新增 177557、177692；更新 122193 |
| PetSkill.yml | 551 | 553 | 新增上述两只宠物的技能；更新 122193 |
| PetStrength.yml | 180 | 185 | 强化名单新增 5 个 ID；102 个已有条目变化，主要是活动概率和日期 |
| PetHuanHua.yml | 18 | 18 | 18 个已有条目的加成日期变化 |
| PetPK.yml | 549 | 551 | 新增两只；保留已有宠物非零属性后，已有 174753 的技能配置有变化 |

PetBase 包含 2 个没有展示配置的关联 ID，所以其总量比 PetSkill 多 2。实际可展示宠物从 551 增为 553。

新增宠物 `177557`、`177692` 的名称未从当前物品配置或本次宠物配置消息中确认，没有猜填名称。其展示和技能数据完整；本次 PK 配置给出的战力、五项属性均为零，不能据此保证新宠物的实际对战效果。

`122193` 在现有 Item.yml 中名为“夏之神祝融”：`CanRide 0→1`、`RidePetType 0→2`、`StrengthFullLevel 0→10`，并新增强化技能值和解锁技能。

强化名单新增 `123397、166153、98790、163368、122193`；其中只有 `122193` 在这批数据中有新增强化等级明细。其他 4 个只捕获到名单项，候选沿用现有 schema 的 `HasDetail: false`，没有编造强化等级。当前完整强化明细为 175 只。

## 文件对应位置

| 本目录候选 | 服务端对应文件 |
| --- | --- |
| PetBase.yml | Server/bin/Pet/PetBase.yml |
| PetSkill.yml | Server/bin/Pet/PetSkill.yml |
| PetStrength.yml | Server/bin/Pet/PetStrength.yml |
| PetHuanHua.yml | Server/bin/Pet/PetHuanHua.yml |
| PetPK.yml | Server/bin/Pet/PetPK.yml |
| Config/PetPK.yml | Server/bin/Config/PetPK.yml |

全部候选使用 UTF-8。更新时需要成套同步，先备份上述原文件；回滚时还原同一套备份。不要把 `Config/PetPK.yml` 与 `Pet/PetPK.yml` 混淆。

源码 `pet.cpp:2414`、`pet.cpp:2434` 的 PK 响应实际读取 `Config/PetPK.yml` 的 `Response21140Hex`、`Response21149HexList`，因此只替换五张结构表不足以同步客户端 PK 配置。候选发包配置与本目录合并后的 PetPK 表一致；其中玩家 UIN 已清零，发送时由服务端写入当前玩家 UIN。

PK 十六进制响应有进程内缓存；本次未验证运行时重新加载流程，不保证直接替换文件会即时生效。后续部署应在维护窗口重新加载/重启后验收，本次未执行这些操作。

## 抓包与合并的区别

抓包的 106 个已有 PK 宠物，其战力、属性变成全零，部分技能说明也为空。尚未确认这是否代表正式数值调整。

- `captured-PetPK.yml` 保留抓包对应字段，供核查，不作为推荐覆盖文件。
- `PetPK.yml` 对这 106 个已有宠物保留本地非零战力、属性，以及相同技能的原说明；继续合并有证据的新增宠物和技能结构变化。
- `Config/PetPK.yml` 从上述合并结果重新编码，因此不是未经修改的抓包原文。
- `pet-diff.json` 的 `configs.PetPK.changed` 是合并保护前的抓包差异；最终合并后的已有条目变化见 `verification.json`，为 `174753`。
- 没有强化明细的旧宠物保留本地未观测字段，新增且无明细的条目仅用 schema 占位，不会构造明细分包。
- `PetBase.LevelExperience` 本次未确认新值，保留原配置。

抓包中的主要强化/幻化加成日期为 **20260801–20260808**，是抓包当时的配置，不是今天仍有效的新活动。候选忠实保存日期，没有擅自延长；实际启用活动前需按你的活动安排检查。

## 验证结果

- 21 份相关宠物消息完整解析并重新编码，326,000 字节与对应抓包一致；包含重复 PK 请求，用于一致性检查。
- 21 份截断消息均被解析器拒绝。
- 候选 YAML 回读通过，跨表 ID、展示顺序、强化顺序和分包数量检查通过。
- 强化卡 ID 与当前 `Config/Pet.yml` 一致。
- 重新解码候选 PK 响应，内容与候选 PetPK 结构表一致；106 个已有宠物的非零值保留验证通过。
- 原五份宠物配置 SHA-256 未变。
- 提取脚本经 AST/危险 API 静态扫描和人工审查，没有外部访问或运行目录写入。

未进行 C++ 构建、服务端加载、新宠物展示、强化或对战实测。这些文件是经过离线校验的更新候选，不是线上效果已验证的配置。

复现命令：

```powershell
python -B F:\PacketDumpTool\21543\server\docs\config-update-2620\extract_pets.py
```

## Map.yml 的补充结论

当前 Map.yml 确实没有 `474 猫梦浮屋`、`477 八闽山海`。本次仍只确认了 ID、名称、新图提示；尚无已验证数据可补齐 `Head/End/Loop/Round/Limit`，因此没有生成可直接替换的 Map.yml。地图 ID 和提示片段位于上一级目录。
