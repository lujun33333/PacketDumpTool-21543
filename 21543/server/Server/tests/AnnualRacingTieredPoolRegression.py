from __future__ import annotations

import pathlib
import re
import sys

import yaml


SERVER_ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG_PATH = SERVER_ROOT / "bin" / "24251" / "AnnualRacing.yml"
SOURCE_PATH = SERVER_ROOT / "MessageQuickRPC.cpp"
DATABASE_HEADER_PATH = SERVER_ROOT / "Database.h"
DATABASE_SOURCE_PATH = SERVER_ROOT / "Database.cpp"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    root = yaml.safe_load(CONFIG_PATH.read_text(encoding="utf-8"))
    annual = root["annual_racing"]
    items = annual["award_pool"]["items"]

    pools: dict[int, list[dict]] = {}
    for level in range(7):
        pools[level] = [
            item for item in items if level in item.get("pool_indexes", [])
        ]
        require(
            len(pools[level]) == 8,
            f"level {level} must expose eight wheel rewards, got {len(pools[level])}",
        )
        require(
            sorted(item["idx"] for item in pools[level]) == list(range(1, 9)),
            f"level {level} award indexes must be exactly 1..8",
        )
        require(
            all(
                item.get("normal_weight", 0) > 0
                and item.get("advanced_weight", 0) > 0
                for item in pools[level]
            ),
            f"level {level} contains a zero-weight reward",
        )

    require(
        [item.get("item_id_1", item["item_id"]) for item in pools[0]]
        == [23674, 23373, 23056, 89847, 112212, 12376, 12378, 12380],
        "level zero no longer matches the captured male-character official pool",
    )
    require(
        pools[0][3]["item_id"] == 89848
        and pools[0][3]["item_id_1"] == 89847,
        "level-zero wing must map female/male IDs correctly",
    )
    require(
        [item["item_id"] for item in pools[6]]
        == [98263, 163289, 106674, 107370, 82248, 88684, 27137, 69936],
        "level six no longer matches the captured official pool",
    )

    published_partial = {
        1: {14107, 28658},
        2: {80542, 102226, 104134, 30809, 87375},
        3: {98263, 163289, 63705, 88863, 96043},
        4: {98263, 163289, 117935, 66598, 101382},
        5: {98263, 163289, 74884, 63482, 118206},
    }
    for level, expected_ids in published_partial.items():
        actual_ids = {item["item_id"] for item in pools[level]}
        require(
            expected_ids <= actual_ids,
            f"level {level} is missing a reward shown on the official page",
        )

    black_mamba = next(item for item in items if item["item_id"] == 98263)
    blessing_stone = next(item for item in items if item["item_id"] == 163289)
    require(
        black_mamba["pool_indexes"] == [3, 4, 5, 6],
        "Black Mamba must retain captured poll_index 3..6",
    )
    require(
        blessing_stone["pool_indexes"] == [3, 4, 5, 6],
        "blessing stone must retain captured poll_index 3..6",
    )

    star_rewards = annual["star_level_rewards"]["levels"]
    require(
        [row["items"][0]["item_id"] for row in star_rewards]
        == [155938, 164097, 164105, 158397, 109562, 163043],
        "six point-light rewards no longer match the captured path",
    )
    require(
        star_rewards[1]["items"][0]["item_id_1"] == 164087
        and star_rewards[2]["items"][0]["item_id_1"] == 164095,
        "point-light clothing rewards must map female/male IDs correctly",
    )

    stone_roll = annual["max_star_advanced"]["blessing_stone"]
    require(
        stone_roll["enabled"] is True,
        "max-star advanced blessing-stone roll must be enabled",
    )
    require(
        stone_roll["chance_per_10000"] == 1000,
        "max-star advanced blessing-stone chance must be the video-derived 10%",
    )
    require(
        stone_roll["grant_non_stone_extra_on_hit"] is True,
        "stone hit must retain an additional non-stone wheel reward",
    )
    require(
        not any(
            "pity" in str(key).lower()
            or "guarantee" in str(key).lower()
            or "miss" in str(key).lower()
            for key in stone_roll
        ),
        "max-star blessing-stone configuration must not contain a hard pity",
    )

    source = SOURCE_PATH.read_text(encoding="utf-8")
    require(
        "AnnualRacingRules::ContainsPool" in source,
        "runtime does not filter rewards by current star",
    )
    require(
        re.search(
            r"AR_PickAward\s*\(\s*cfg\s*,\s*state\.StarLevel\s*,"
            r"\s*advanced\s*,\s*state\.SageEggGetNum\s*,"
            r"\s*useMaxStarStoneRoll\s*\)",
            source,
        )
        is not None,
        "draw path does not select the current-star pool",
    )
    require(
        "AnnualRacingRules::ShouldGrantFirstLight" in source,
        "draw path does not protect one-time point-light rewards",
    )
    require(
        "AnnualRacingRules::ShouldCountBlessingStone" in source,
        "draw path does not count blessing stones from random hits",
    )
    require(
        "extraAwards.push_back(sageReward)" not in source,
        "draw path still grants a guaranteed extra blessing stone",
    )
    require(
        "AnnualRacingRules::IsMaxStarAdvancedBlessingStoneEligible" in source,
        "draw path does not scope the random stone roll to max-star advanced draws",
    )
    require(
        "AnnualRacingRules::HitsPerTenThousandChance" in source,
        "draw path does not apply the configured random stone chance",
    )
    require(
        "extraAwards.push_back(companionReward)" in source,
        "stone hit does not return the additional non-stone reward",
    )
    require(
        "ShouldForceBlessingStonePity" not in source
        and "NextBlessingStonePityMissCount" not in source,
        "draw path still contains blessing-stone hard-pity logic",
    )
    require(
        "for (size_t poolIndex = 0; poolIndex < item.PoolIndexes.size(); ++poolIndex)"
        in source,
        "config response does not serialize full poll_index membership",
    )

    database_header = DATABASE_HEADER_PATH.read_text(encoding="utf-8")
    database_source = DATABASE_SOURCE_PATH.read_text(encoding="utf-8")
    require(
        "StonePityMissCount" not in database_header
        and "StonePityMissCount" not in database_source,
        "database must not persist a blessing-stone hard-pity counter",
    )

    print("AnnualRacingTieredPoolRegression passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"AnnualRacingTieredPoolRegression failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
