from pathlib import Path


SERVER = Path(__file__).resolve().parents[1]
MESSAGE = (SERVER / "Message.cpp").read_text(encoding="utf-8-sig")
DATABASE = (SERVER / "Database.cpp").read_text(encoding="utf-8-sig")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# ServerLimit belongs to treasure draws. Generic inventory grants are shared by
# CDKs, tasks, APIs, shops, and many other systems and must not consult it.
require(
    "Treasure_BlockSoldOutAdd" not in DATABASE,
    "generic PlayerDB_AddItem functions still enforce treasure ServerLimit",
)
require(
    "Treasure_BlockSoldOutAdd" not in MESSAGE,
    "the old cross-system ServerLimit gate still exists",
)

# Both the modern box-168 flow and the classic treasure flow must continue to
# filter sold-out awards and claim quota inside their own draw transactions.
require(
    "SB168_IsSoldOut" in MESSAGE
    and "Treasure_TryClaimServerLimit(limitConn, cfg.BoxID" in MESSAGE,
    "box-168 no longer enforces ServerLimit",
)
require(
    MESSAGE.count("Treasure_EraseSoldOutServerLimit(") >= 3,
    "classic treasure draws no longer filter sold-out awards",
)
require(
    MESSAGE.count("Treasure_TryClaimServerLimit(conn, Itemid") >= 2,
    "classic treasure draws no longer claim ServerLimit quota",
)

# CDK delivery intentionally uses the unrestricted generic inventory path.
cdk_start = MESSAGE.index("BOOL SendPwdRedEnvelope(")
cdk_end = MESSAGE.index("int SendAnyItem(", cdk_start)
cdk_source = MESSAGE[cdk_start:cdk_end]
require(
    "PlayerDB_AddItem1(" in cdk_source,
    "CDK delivery no longer uses the generic inventory grant path",
)

print("PASS: ServerLimit is scoped to treasure draws; CDK grants are unrestricted")
