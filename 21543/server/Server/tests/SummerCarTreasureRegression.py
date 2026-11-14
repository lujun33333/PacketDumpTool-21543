import hashlib
import json
from pathlib import Path

import yaml


SERVER = Path(__file__).resolve().parents[1]
CONFIG = SERVER / "bin" / "24251" / "SummerCarTreasure.yml"
QUICK_RPC = SERVER / "MessageQuickRPC.cpp"
MESSAGE = SERVER / "Message.cpp"
GAME_SERVER = SERVER / "GameServer.cpp"
DATABASE = SERVER / "Database.cpp"


def load_config():
    document = yaml.safe_load(CONFIG.read_text(encoding="utf-8"))
    return document["summer_car_treasure"]


def group_types(awards):
    result = {}
    for award in awards:
        group = award.get("group", 0)
        select_type = award.get("select", 0)
        if not group:
            assert select_type == 0
            continue
        assert select_type in (1, 2, 3)
        assert result.setdefault(group, select_type) == select_type
    return result


def official_award_shape(award, summer):
    row = {
        "itemNum": award.get("num", 1),
        "itemType": award.get("type", 1),
        "iAvailPeriod": award.get("period", -1),
        "iCanExchange": award.get("exchange", 0),
        "iOtherAttribute": award.get("attr", 1),
        "iProbability": award.get("weight", 1),
        "itemId": award["id"],
    }
    if award.get("group", 0):
        row["iGroupIndex"] = award["group"]
    if award.get("piece", 0):
        row["iExchangePieceNum"] = award["piece"]
    if award.get("select", 0):
        row["iSelectCarType"] = award["select"]
    if summer:
        row["iBelongSummerAwardPoll"] = 1
    return row


def pool_digest(awards, summer):
    rows = [official_award_shape(award, summer) for award in awards]
    payload = json.dumps(rows, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(payload).hexdigest()


def main():
    cfg = load_config()
    normal = cfg["normal"]
    carnival = cfg["carnival"]
    crystal_shop = cfg["crystal_shop"]

    assert set(cfg) == {
        "enabled",
        "activity_id",
        "date",
        "ticket",
        "select",
        "progress",
        "crystal_shop",
        "normal",
        "carnival",
    }
    assert cfg["enabled"] is True
    assert cfg["activity_id"] == 202607
    assert cfg["date"] == {"open": 20260704, "close": 20260731}
    assert cfg["ticket"] == {"id": 99662, "piece": 94880}
    assert cfg["select"] == {"s": 3, "t": 2, "a": 7}
    assert cfg["progress"] == {
        "luck": 800,
        "crystal": 450,
        "crystal_item": 144343,
        "crystal_times": 5,
    }
    assert [
        (row["id"], row["cost"]) for row in crystal_shop
    ] == [
        (150528, 5), (132210, 5), (157768, 5), (146550, 5),
        (130778, 5), (144523, 5), (116735, 5), (110709, 3),
        (146554, 5), (117934, 5), (150531, 5), (150529, 5),
        (146546, 5), (122199, 5), (117941, 5), (110711, 5),
        (133688, 5), (132211, 5), (146552, 5), (146553, 5),
        (150530, 5), (153688, 5), (138418, 5), (123502, 5),
        (140032, 5), (133689, 5), (120657, 5), (113415, 5),
        (146547, 5), (135171, 5), (112178, 2), (113414, 2),
        (116740, 2), (132227, 2), (132228, 2),
    ]
    assert normal["cost"] == [1, 10]
    assert carnival["cost"] == [3, 30]
    assert len(normal["awards"]) == 70
    assert len(carnival["awards"]) == 62
    assert normal["awards"][0]["id"] == 119059
    assert normal["awards"][-1]["id"] == 96597
    assert carnival["awards"][0]["id"] == 88865
    assert carnival["awards"][-1]["id"] == 96597
    # 由 41440 赛车夺宝配置响应逐项解码后生成的语义摘要。
    assert pool_digest(normal["awards"], False) == (
        "1babd364654ddd402a4b30c560512a4069b8dffd6badc29b77db896efafebeef"
    )
    assert pool_digest(carnival["awards"], True) == (
        "2fb410579c2211e402030059a0834eea6b83644769366af3af039adc2dbbcea2"
    )

    for pool in (normal["awards"], carnival["awards"]):
        for award in pool:
            assert award["id"] > 0
            assert award.get("num", 1) > 0
            assert award.get("weight", 1) > 0
            assert award.get("type", 1) in (0, 1)
            assert award.get("period", -1) >= -1

    types = group_types(normal["awards"])
    assert sum(value == 1 for value in types.values()) == 10
    assert sum(value == 3 for value in types.values()) == 4
    assert sum(value == 2 for value in types.values()) == 10
    assert all(award.get("group", 0) == 0 for award in carnival["awards"])
    # 普通池的 iExchangePieceNum 是分解所得能量，不是 432 商店价格。
    exchange_rows = [
        award for award in normal["awards"] if award.get("exchange", 0) == 1
    ]
    assert len(exchange_rows) == 48
    assert all(award.get("piece", 0) > 0 for award in exchange_rows)
    assert next(
        row["piece"]
        for row in exchange_rows
        if row["id"] == 106678 and row.get("period", -1) == -1
    ) == 180
    assert next(
        row["shop"]
        for row in exchange_rows
        if row["id"] == 106678 and row.get("period", -1) == -1
    ) == 550
    assert all(row.get("shop", 0) > 0 for row in exchange_rows)

    # 官服首次选择抓包：3S + 2T + 7A，slot 与 group 均不可重复。
    selected = [
        1, 1, 2, 2, 3, 3,
        4, 14, 5, 13,
        6, 21, 7, 22, 8, 23, 9, 24, 10, 16, 11, 20, 12, 15,
    ]
    slots = selected[0::2]
    groups = selected[1::2]
    assert slots == list(range(1, 13))
    assert len(set(groups)) == 12
    assert [types[group] for group in groups[:3]] == [1, 1, 1]
    assert [types[group] for group in groups[3:5]] == [3, 3]
    assert [types[group] for group in groups[5:]] == [2] * 7

    # 官服 24465 Type=432：按已选 group 动态返回12辆车，每辆永久+限时。
    # 当前实包余额350、已永久拥有88865；PosKey/AwardIdx 保留官服槽位。
    by_group = {}
    for award in exchange_rows:
        by_group.setdefault(award["group"], []).append(award)
    selected_groups = sorted(groups)
    official_rows = []
    deferred_group_one = None
    for group in selected_groups:
        awards = by_group[group]
        permanent = next(row for row in awards if row.get("period", -1) == -1)
        temporary = next(
            row for row in awards if row.get("period", -1) > 0
        )
        permanent_pos = 1 if group == 1 else (group - 1) * 2
        temporary_pos = 0 if group == 1 else permanent_pos + 1
        owned = permanent["id"] == 88865
        official_rows.append(
            (
                permanent_pos,
                permanent["id"],
                permanent["shop"],
                -1,
                0 if owned or permanent["shop"] > 350 else 1,
                11506 if owned else (11498 if permanent["shop"] > 350 else 0),
                1 if owned else 0,
            )
        )
        temporary_row = (
            temporary_pos,
            temporary["id"],
            temporary["shop"],
            temporary["period"],
            0 if owned or temporary["shop"] > 350 else 1,
            11506 if owned else (11498 if temporary["shop"] > 350 else 0),
            1 if owned else 0,
        )
        if group == 1:
            deferred_group_one = temporary_row
        else:
            official_rows.append(temporary_row)
    official_rows.append(deferred_group_one)
    assert official_rows == [
        (1, 88865, 2000, -1, 0, 11506, 1),
        (2, 106675, 1500, -1, 0, 11498, 0),
        (3, 106675, 100, 360, 1, 0, 0),
        (4, 74362, 1500, -1, 0, 11498, 0),
        (5, 74362, 100, 360, 1, 0, 0),
        (24, 106678, 550, -1, 0, 11498, 0),
        (25, 106678, 55, 720, 1, 0, 0),
        (26, 93573, 550, -1, 0, 11498, 0),
        (27, 93573, 55, 720, 1, 0, 0),
        (28, 136727, 550, -1, 0, 11498, 0),
        (29, 136727, 55, 720, 1, 0, 0),
        (30, 148401, 350, -1, 1, 0, 0),
        (31, 148401, 20, 720, 1, 0, 0),
        (38, 119076, 350, -1, 1, 0, 0),
        (39, 119076, 20, 720, 1, 0, 0),
        (40, 157757, 350, -1, 1, 0, 0),
        (41, 157757, 20, 720, 1, 0, 0),
        (42, 150542, 350, -1, 1, 0, 0),
        (43, 150542, 20, 720, 1, 0, 0),
        (44, 102230, 350, -1, 1, 0, 0),
        (45, 102230, 20, 720, 1, 0, 0),
        (46, 106684, 350, -1, 1, 0, 0),
        (47, 106684, 20, 720, 1, 0, 0),
        (0, 88865, 100, 360, 0, 11506, 1),
    ]

    quick_rpc = QUICK_RPC.read_text(encoding="utf-8")
    start = quick_rpc.index("// SummerCarTreasure /")
    end = quick_rpc.index("NOINLINE int CarTreasureCfgData", start)
    summer_source = quick_rpc[start:end]
    assert "SCT_PickWeighted" in summer_source
    assert "std::uniform_int_distribution<std::uint64_t>" in summer_source
    assert "CT_LoadFixedDrawSampleItems" not in summer_source
    assert "Sleep(" not in summer_source
    assert "Forced=0 Delay=0" in summer_source

    writer_start = summer_source.index("static void SCT_WriteConfigData")
    writer_end = summer_source.index("static std::vector<SummerCarTreasureDbAwardRow>", writer_start)
    writer = summer_source[writer_start:writer_end]
    official_field_order = (
        "iCanResetSelect",
        "iHasTiyanItemToRecv",
        "iCurrentLuckValue",
        "iOpenTime",
        "iCanOpenTiYan",
        "iTotalLuckValue",
        "iCloseTime",
        "iThemeId",
        "iSkin2IsOpen",
        "score_discount_cfg",
        "iInnerCostNumOneTime",
        "iCostItemID",
        "iTiyanCloseTime",
        "iCostNumOneTime",
        "iDiscountBeginTime",
        "iTCarCanSelectNum",
        "iExchangePieceItemID",
        "iDiscountCloseDate",
        "iOpenDate",
        "CarTreasureTiyanItem",
        "iNeedToSelectCar",
        "iTiyanOpenTime",
        "RetRecItemTable",
        "RetComItemTable",
        "award_poll_cfg",
        "iCanOpenSummerCarTreasure",
        "iJingShiReachLimit",
        "iJingShiCanGotTotalNum",
        "iJingShiTotalLuckyProgress",
        "iDiscountBeginDate",
        "iJingShiProgress",
        "iACarCanSelectNum",
        "iSkin2Id",
        "iSkin3IsOpen",
        "iSkin3Id",
        "iDiscountCostNumTenTime",
        "iAlreadySelectCarGroupIndex",
        "UseDiscountNum",
        "iDiscountCloseTime",
        "coupons_index",
        "iTicketEligibilityDesc",
        "iInnerCaostNumTenTime",
        "isInSpecialPeriod",
        "iCloseDate",
        "RetDecpItemTable",
        "iJingShiGotNum",
        "iSkin1IsOpen",
        "iTiyanOpenDate",
        "i227CanSelectNum",
        "iUIVersion",
        "iCaostNumTenTimeAfterDiscount",
        "iSCarCanSelectNum",
        "iCaostNumTenTime",
        "iInnerAwardPoolThresholdDate",
        "iInnerAwardPoolThresholdEndDate",
    )
    cursor = -1
    for field in official_field_order:
        token = "SCT_WriteAwardPool(" if field == "award_poll_cfg" else f'"{field}"'
        cursor = writer.index(token, cursor + 1)
    assert "hasPermanentItemList" not in writer
    assert "state.DiscountUsedNum > 0 ? 1 : 0" in writer
    assert "iCanExchange" in summer_source
    assert "iExchangePieceNum" in summer_source
    assert "packet_dumps_" not in summer_source
    assert ".bin" not in summer_source
    assert "ShopCost" in summer_source
    assert "struct SummerTreasureCrystalShopItemCfg" in summer_source
    assert 'CT_YInt(row, "shop", 0)' in summer_source
    assert 'root["exchange"]' not in summer_source
    assert 'root["crystal_shop"]' in summer_source
    assert "kSummerTreasureExchangeType = 432" in summer_source
    assert "kSummerTreasureCrystalShopType = 433" in summer_source
    assert "kSummerTreasureCrystalFirstPageSize = 26" in summer_source
    assert "SummerCarTreasure_HandleCommonBoxPreview" in summer_source
    assert "SummerCarTreasure_TryResolveCommonBoxRow" in summer_source

    message = MESSAGE.read_text(encoding="utf-8")
    for command in (
        "Co.RequestGetSummerCarTreasureCfg",
        "Co.RequestGetSummerCarTreasureAward",
        "Co.Beta96_RequestSelectGroupIndex",
    ):
        assert command in message
    assert "Co.Beta96_Speed_Coin_Time_Limit_Exchange_" not in message
    assert "SummerCarTreasure_TryResolveCommonBoxRow" in message
    assert "PlayerDB_TryConsumeItem" in message

    game_server = GAME_SERVER.read_text(encoding="utf-8")
    assert "SummerCarTreasure_IsCommonBoxType(Type)" in game_server
    assert "SummerCarTreasure_HandleCommonBoxPreview" in game_server

    database = DATABASE.read_text(encoding="utf-8")
    assert "CREATE TABLE IF NOT EXISTS SummerCarTreasureState" in database
    assert "PlayerDB_ApplySummerCarTreasureDraw" in database
    assert "WHERE Uin=? AND ItemID=? AND ItemNum>=?" in database
    assert "SummerCarTreasureShopState" not in database
    assert "PlayerDB_ApplySummerCarTreasureShopExchange" not in database

    print(
        "SummerCarTreasure regression passed: "
        "normal=70 carnival=62 exchange432=24 crystal433=35 "
        "select=3S+2T+7A forced=0 delay=0"
    )


if __name__ == "__main__":
    main()
