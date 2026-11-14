$ErrorActionPreference = 'Stop'

$config = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\bin\Config\SingleSkillStoneCfg.yml'))
if (-not (Test-Path -LiteralPath $config)) {
    throw "runtime skill-stone config not found: $config"
}

$python = @'
import hashlib
import sys
from pathlib import Path

import yaml

path = Path(sys.argv[1])
document = yaml.safe_load(path.read_text(encoding="utf-8"))
rows = document.get("SingleSkillStoneCfg")
if not isinstance(rows, list) or len(rows) != 84:
    raise SystemExit(f"expected 84 configured Msg800 rows, got {0 if rows is None else len(rows)}")

fields = (
    "StoneID",
    "ActiveCondVal1",
    "UseCountUpperlimit",
    "ActiveSuccessProb",
    "CoolTime",
    "GenResultVal1",
)
normalized = "\n".join(",".join(str(int(row[field])) for field in fields) for row in rows)
actual_hash = hashlib.sha256(normalized.encode("ascii")).hexdigest()
# 官服 LV1-LV4 与其余保留值不变；重生宝珠 LV6 使用当前用户配置的 60%/5秒。
expected_hash = "2e72108e3fc7c31a082ee74f3acdfdd8b5e62bfc8ea205c2019bfa5bdc68145e"
if actual_hash != expected_hash:
    raise SystemExit(f"preserved runtime Msg800 table hash mismatch: {actual_hash}")

# 14 组宝石均按 LV1-LV6 排列；每组前四条必须与官服 21487 Msg800 完全一致。
level_1_4_rows = [row for index, row in enumerate(rows) if index % 6 < 4]
level_1_4_normalized = "\n".join(
    ",".join(str(int(row[field])) for field in fields)
    for row in level_1_4_rows
)
level_1_4_hash = hashlib.sha256(level_1_4_normalized.encode("ascii")).hexdigest()
expected_level_1_4_hash = "3c63fbb76a94810b6beef9d362d36f975d3e59e0e46b6be0bbc3cc0a1e533066"
if level_1_4_hash != expected_level_1_4_hash:
    raise SystemExit(f"official Msg800 level 1-4 table hash mismatch: {level_1_4_hash}")

by_id = {int(row["StoneID"]): row for row in rows}
expected_rebirth = {
    21983: (1000, 1, 30, 10, 0),
    21984: (1000, 2, 80, 10, 0),
    21985: (1000, 4, 130, 10, 0),
    21986: (1000, 6, 190, 10, 0),
    21987: (1000, 8, 290, 10, 0),
    21988: (1000, 10, 600, 5, 0),
}
for stone_id, expected in expected_rebirth.items():
    row = by_id.get(stone_id)
    if row is None:
        raise SystemExit(f"missing rebirth stone row {stone_id}")
    actual = tuple(int(row[field]) for field in fields[1:])
    if actual != expected:
        raise SystemExit(f"rebirth row {stone_id} mismatch: {actual} != {expected}")

# These are the level 5/6 rows that are non-zero in the runtime configuration.
# Keep them explicit so a future accidental change reports the affected stone.
expected_level_5_6 = {
    21981: (750, 8, 800, 20, 0),
    21982: (700, 10, 900, 20, 0),
    21987: (1000, 8, 290, 10, 0),
    21988: (1000, 10, 600, 5, 0),
    21993: (1000, 8, 800, 10, 0),
    21994: (1000, 10, 1000, 10, 0),
    22017: (40, 8, 1000, 10, 0),
    22018: (0, 10, 1000, 10, 0),
    22029: (1000, 9, 300, 8, 0),
    22030: (1000, 11, 350, 8, 0),
    22035: (1000, 9, 300, 8, 0),
    22036: (1000, 11, 350, 8, 0),
    22041: (1000, 6, 220, 5, 0),
    22042: (1000, 7, 290, 5, 0),
    22047: (1000, 9, 230, 10, 0),
    22048: (1000, 11, 300, 10, 0),
}
for stone_id, expected in expected_level_5_6.items():
    row = by_id.get(stone_id)
    if row is None:
        raise SystemExit(f"missing preserved level 5/6 stone row {stone_id}")
    actual = tuple(int(row[field]) for field in fields[1:])
    if actual != expected:
        raise SystemExit(f"level 5/6 row {stone_id} mismatch: {actual} != {expected}")

item_ids = [int(item["ItemId"]) for item in document.get("ItemCfg", [])]
if item_ids != [18554, 18553]:
    raise SystemExit(f"existing ItemCfg was not preserved: {item_ids}")

server_activation = document.get("ServerActivation")
if not isinstance(server_activation, dict) or int(server_activation.get("ProbabilityScale", 0)) != 1000:
    raise SystemExit("missing official server-side probability scale")
server_rules = server_activation.get("Rules")
expected_server_rule = {
    "SkillType": 1,
    "TriggerPropID": 1,
    "StoneIDs": [21983, 21984, 21985, 21986, 21987, 21988],
}
if not isinstance(server_rules, list) or server_rules != [expected_server_rule]:
    raise SystemExit(f"unexpected server-side skill-stone rules: {server_rules}")

print(f"SkillStoneConfigRegression: PASS rows={len(rows)} hash={actual_hash}")
'@

$python | python - $config
if ($LASTEXITCODE -ne 0) {
    throw "skill-stone config regression failed with exit code $LASTEXITCODE"
}
