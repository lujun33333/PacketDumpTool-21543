# Summer Car Treasure Configuration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 先交付可由后续 C++ 结构体直接加载的官服 `SummerCarTreasure.yml`，完整保存普通和狂欢两个奖池及首次自选规则。

**Architecture:** 配置使用独立根节点 `summer_car_treasure`，不覆盖老版 `CarTreasure.yml`。静态活动字段和两个标准化奖池分别来自 24251 官服响应 context 136、137；玩家进度、自选结果和暂存奖励只定义默认值及规则，不把抓包玩家状态写成固定结果。

**Tech Stack:** YAML 1.2、PyYAML 6、QQ飞车 24251 QuickRPC、PowerShell、Python 3。

## Global Constraints

- 只创建 `server/Server/bin/24251/SummerCarTreasure.yml`；本阶段不修改 C++、数据库和老版 `CarTreasure.yml`。
- 普通奖池必须为官服抓包中的 70 行，狂欢奖池必须为官服抓包中的 62 行。
- 抽奖仅保存正整数权重；不配置固定奖励、固定 seed、额外十连保底或进度强制中奖。
- 首次自选数量固定为 S 车 3 组、T 车 2 组、A 车 7 组；抓包玩家选择不得成为默认选择。
- `Co.RequestGetSummerCarTreasureCfg`、`Co.RequestGetSummerCarTreasureAward` 和 `Co.Beta96_RequestSelectGroupIndex` 与老版方法并存。
- 官服证据文件保持只读，生成配置后必须逐字段反向比对。

---

### Task 1: 创建并核验官服 YAML 配置

**Files:**
- Create: `server/Server/bin/24251/SummerCarTreasure.yml`
- Read: `F:/PacketDumpTool/PacketDumpTool/x64/Release/packet_dumps_17072/payloads/network_bin_17072_22938_special28346_before_inner_body_at12_msg24251_len10102.bin`
- Read: `F:/PacketDumpTool/PacketDumpTool/x64/Release/packet_dumps_17072/payloads/network_bin_17072_22965_special28346_before_inner_body_at12_msg24251_len8834.bin`

**Interfaces:**
- Consumes: QuickRPC context 136 的普通配置和 context 137 的狂欢配置。
- Produces: `summer_car_treasure` 根节点；后续 C++ 加载器按 `protocol`、`time`、`cost`、`select_car`、`progress`、`client`、`operations`、`normal_pool`、`carnival_pool` 读取。

- [x] **Step 1: 验证目标配置尚不存在**

```powershell
if (Test-Path -LiteralPath 'F:\PacketDumpTool\21487\server\Server\bin\24251\SummerCarTreasure.yml') {
    throw 'SummerCarTreasure.yml already exists; review instead of overwriting'
}
```

Expected: 命令成功结束且不覆盖任何已有文件。

- [x] **Step 2: 从两个官服响应生成标准化配置**

使用 `F:\new1\qqspeed_decoder\quickrpc.py` 解开 QuickRPC body，再按全局 `FA/FB` 字符串表、`F6` 数值、`F8/F9` 布尔值和 `FD` 定长块解析。把 `award_poll_cfg` 映射为：

```yaml
- index: 1
  item_id: 119059
  item_num: 1
  item_type: 1
  avail_period: -1
  group_index: 0
  can_exchange: 0
  other_attribute: 1
  exchange_piece_num: 1500
  probability: 1
  select_car_type: 0
  belong_summer_pool: 0
```

未在某行 wire 中出现的标准字段写入 `0`，从而让每个 YAML 奖励都能直接构造成同一个结构体。普通池读取 context 136 的全部 70 行；狂欢池读取 context 137 的全部 62 行。

- [x] **Step 3: 验证 YAML 结构和业务不变量**

```powershell
@'
from pathlib import Path
import yaml

p = Path(r"F:\PacketDumpTool\21487\server\Server\bin\24251\SummerCarTreasure.yml")
root = yaml.safe_load(p.read_text(encoding="utf-8"))["summer_car_treasure"]
assert root["protocol"]["quickrpc_msgid"] == 24251
assert root["protocol"]["cfg_cmd"] == "Co.RequestGetSummerCarTreasureCfg"
assert root["protocol"]["award_cmd"] == "Co.RequestGetSummerCarTreasureAward"
assert root["protocol"]["select_cmd"] == "Co.Beta96_RequestSelectGroupIndex"
assert root["select_car"]["s_count"] == 3
assert root["select_car"]["t_count"] == 2
assert root["select_car"]["a_count"] == 7
assert root["select_car"]["default_selected_groups"] == []
assert len(root["normal_pool"]["awards"]) == 70
assert len(root["carnival_pool"]["awards"]) == 62
for pool in ("normal_pool", "carnival_pool"):
    rows = root[pool]["awards"]
    assert [row["index"] for row in rows] == list(range(1, len(rows) + 1))
    assert all(row["item_id"] > 0 and row["item_num"] > 0 for row in rows)
    assert all(row["probability"] > 0 for row in rows)
assert all(row["belong_summer_pool"] == 0 for row in root["normal_pool"]["awards"])
assert all(row["belong_summer_pool"] == 1 for row in root["carnival_pool"]["awards"])
print("SummerCarTreasure.yml schema OK: normal=70 carnival=62")
'@ | python -
```

Expected: `SummerCarTreasure.yml schema OK: normal=70 carnival=62`

- [x] **Step 4: 与官服响应逐行反向比对**

重新解析 context 136、137，把每行 wire 字段按下列映射标准化，然后要求列表完全相等：

```text
itemId                  -> item_id
itemNum                 -> item_num
itemType                -> item_type
iAvailPeriod            -> avail_period
iGroupIndex             -> group_index
iCanExchange            -> can_exchange
iOtherAttribute         -> other_attribute
iExchangePieceNum       -> exchange_piece_num
iProbability            -> probability
iSelectCarType          -> select_car_type
iBelongSummerAwardPoll  -> belong_summer_pool
```

Expected: 普通池 `70/70`、狂欢池 `62/62` 全字段一致，没有额外或缺失行。

- [x] **Step 5: 检查差异并提交配置**

```powershell
git -C 'F:\PacketDumpTool\21487' diff --check -- `
  'server/Server/bin/24251/SummerCarTreasure.yml'
git -C 'F:\PacketDumpTool\21487' add -- `
  'server/Server/bin/24251/SummerCarTreasure.yml'
git -C 'F:\PacketDumpTool\21487' commit -m `
  'feat: add official summer car treasure config'
```

Expected: 提交只包含新 YAML；老版配置和业务源码没有变化。
