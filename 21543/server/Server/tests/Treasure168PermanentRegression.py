from __future__ import annotations

from functools import lru_cache
from pathlib import Path

import numpy as np
import yaml


SERVER_DIR = Path(__file__).resolve().parents[1]
MESSAGE_CPP = SERVER_DIR / "Message.cpp"
DATABASE_CPP = SERVER_DIR / "Database.cpp"
CONFIG_PATH = SERVER_DIR / "bin" / "Treasure" / "138121.yml"
SEED_SCALE = 10_000_000
TRIALS_PER_RUN = 100_000
RNG_SEEDS = (20260806, 20260807, 20260808)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


message_source = MESSAGE_CPP.read_text(encoding="utf-8")
database_source = DATABASE_CPP.read_text(encoding="utf-8")

require('".\\\\Treasure\\\\" + fileName' in message_source,
        "msg168 does not search Treasure\\<BoxID>.yml")
require("ownedPermanentItems.find(candidate.ItemID)" in message_source,
        "random awards do not filter an owned permanent ItemID")
require("if (award.AvailPeriod == -1) ownedPermanentItems.insert(award.ItemID);" in message_source,
        "a permanent won in the same multi-open is not added to the exclusion set")
require("!alreadyPermanent && award.AvailPeriod == -1" in message_source,
        "msg168 horn is not restricted to newly obtained permanent awards")
require("items[j].ItemType == 2 && items[j].AvailPeriod == -1" in message_source,
        "legacy random-box horn is not restricted to permanent awards")

require("SELECT ItemNum,AvailPeriod FROM Item" in database_source,
        "PlayerDB_AddItem2 does not read the old period")
require("else if (AvailPeriod > 0 && OldAvailPeriod != -1)" in database_source,
        "PlayerDB_AddItem2 can still replace a permanent period with a timed period")
require("AND AvailPeriod <> -1" in database_source,
        "database update is missing its permanent-period guard")

config = yaml.safe_load(CONFIG_PATH.read_text(encoding="utf-8"))
pools = config["Pool"]
require(len(pools) == 5, "138121.yml must contain four special pools and one normal pool")

previous_seed = 0
start_probabilities: list[float] = []
for pool in pools:
    seed = int(pool["Seed"])
    require(previous_seed < seed <= SEED_SCALE, "pool seeds must be ascending cumulative values")
    start_probabilities.append((seed - previous_seed) / SEED_SCALE)
    previous_seed = seed
require(previous_seed == SEED_SCALE, "the last pool seed must be 10000000")

permanent_ids: list[int] = []
timed_ids: set[int] = set()
bucket_counts = {(2, -1): 0, (2, 720): 0, (1, -1): 0, (1, 720): 0}
for pool in pools:
    item_type = int(pool.get("ItemType", 0))
    for item in pool["Items"]:
        item_id = int(item["ItemId"])
        period = int(item.get("AvailPeriod", 0))
        if (item_type, period) in bucket_counts:
            bucket_counts[(item_type, period)] += 1
        if item_type in (1, 2) and period == -1 and item_id not in permanent_ids:
            permanent_ids.append(item_id)
        if item_type in (1, 2) and period > 0:
            timed_ids.add(item_id)

require(bucket_counts == {(2, -1): 4, (2, 720): 4, (1, -1): 4, (1, 720): 4},
        f"unexpected permanent/timed pool structure: {bucket_counts}")
require(set(permanent_ids) == timed_ids,
        "every special permanent item must have a timed version with the same ItemID")

bit_by_item = {item_id: index for index, item_id in enumerate(permanent_ids)}
full_mask = (1 << len(permanent_ids)) - 1


@lru_cache(maxsize=None)
def transitions(mask: int) -> tuple[float, tuple[float, ...]]:
    """Mirror SB168: one candidate per pool, then fall through if its ItemID is owned."""
    add_probabilities = [0.0] * len(permanent_ids)
    same_probability = 0.0

    for first_pool, start_probability in enumerate(start_probabilities):
        carry = start_probability
        for pool_index in range(first_pool, len(pools)):
            items = pools[pool_index]["Items"]
            blocked = 0
            for item in items:
                item_id = int(item["ItemId"])
                bit_index = bit_by_item.get(item_id)
                is_owned = bit_index is not None and bool(mask & (1 << bit_index))
                if is_owned:
                    blocked += 1
                elif int(item.get("AvailPeriod", 0)) == -1 and bit_index is not None:
                    add_probabilities[bit_index] += carry / len(items)
                else:
                    same_probability += carry / len(items)

            carry *= blocked / len(items)
            if blocked < len(items):
                carry = 0.0
                break
        same_probability += carry

    same_probability += 1.0 - same_probability - sum(add_probabilities)
    return same_probability, tuple(add_probabilities)


expected_draws = [0.0] * (full_mask + 1)
for mask in range(full_mask - 1, -1, -1):
    _, add_probabilities = transitions(mask)
    collect_probability = sum(add_probabilities)
    require(collect_probability > 0.0, f"state {mask:#x} can never collect another permanent item")
    expected_draws[mask] = (
        1.0
        + sum(
            probability * expected_draws[mask | (1 << bit_index)]
            for bit_index, probability in enumerate(add_probabilities)
        )
    ) / collect_probability

require(9_500 <= expected_draws[0] <= 10_500,
        f"exact expected completion is outside the 10000-box target: {expected_draws[0]:.2f}")


def simulate(seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    totals = np.empty(TRIALS_PER_RUN, dtype=np.int64)
    for trial in range(TRIALS_PER_RUN):
        mask = 0
        draws = 0
        while mask != full_mask:
            _, add_probabilities = transitions(mask)
            collect_probability = sum(add_probabilities)
            draws += int(rng.geometric(collect_probability))
            choice = rng.random() * collect_probability
            cumulative = 0.0
            for bit_index, probability in enumerate(add_probabilities):
                cumulative += probability
                if choice < cumulative:
                    mask |= 1 << bit_index
                    break
        totals[trial] = draws
    return totals


print(f"config={CONFIG_PATH}")
print(f"exact_mean={expected_draws[0]:.2f}")
for rng_seed in RNG_SEEDS:
    result = simulate(rng_seed)
    mean = float(np.mean(result))
    median = int(np.median(result))
    p90 = int(np.quantile(result, 0.90, method="nearest"))
    within_target = float(np.mean(result <= 10_000) * 100.0)
    require(abs(mean - expected_draws[0]) / expected_draws[0] < 0.03,
            f"simulation mean drifted too far for RNG seed {rng_seed}: {mean:.2f}")
    print(
        f"seed={rng_seed} trials={TRIALS_PER_RUN} mean={mean:.2f} "
        f"median={median} p90={p90} within_10000={within_target:.2f}%"
    )

print("PASS: msg168 permanent exclusion, permanent-only horn, DB guard, and 10000-box target")
