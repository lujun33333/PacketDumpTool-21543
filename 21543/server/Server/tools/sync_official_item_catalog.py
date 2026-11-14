"""Reconcile ItemAllNew with local official data; never edit player inventory.

Prepare is read-only against MySQL. Apply requires the prepared snapshot, backs
up the full table, locks it in one InnoDB transaction, and verifies every row.
"""
import argparse
import collections
import configparser
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import urllib.parse
import zlib

import pymysql

FIELDS = ("ID", "Type", "Mess", "Name", "BuyFlag", "GameType", "lockid")
ITEM_KEY = b"!G#&^* 9"
SHOP_KEY = b"?Q#!^* 8"
BABY_IDS = {71554, 71555, 82238, 82239}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def packed(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":")).encode("utf-8")


def write_json(path, value):
    with path.open("xb") as f:
        f.write(packed(value))
        f.flush()
        os.fsync(f.fileno())
    if json.loads(path.read_bytes()) != value:
        raise ValueError("Artifact readback mismatch")


def parse_items(data):
    if len(data) < 16:
        raise ValueError("Truncated itemlist")
    version = struct.unpack_from("<I", data)[0]
    index = struct.unpack_from("<I", data, len(data) - 8)[0]
    if not 4 <= index <= len(data) - 12:
        raise ValueError("Invalid item index")
    count = struct.unpack_from("<I", data, index)[0]
    if count > 1_000_000 or index + 4 + count * 8 > len(data) - 8:
        raise ValueError("Invalid item index size")
    pairs = [struct.unpack_from("<II", data, index + 4 + i * 8)
             for i in range(count)]
    if len({p[0] for p in pairs}) != count or len({p[1] for p in pairs}) != count:
        raise ValueError("Duplicate item ID or offset")
    offsets = sorted(p[1] for p in pairs)
    if not offsets or offsets[0] != 4:
        raise ValueError("Unexpected first item offset")
    ends = dict(zip(offsets, offsets[1:] + [index]))
    rows = []
    for item_id, offset in pairs:
        end = ends[offset]
        if not 4 <= offset < end <= index or offset + 55 > end:
            raise ValueError("Item record boundary")
        if struct.unpack_from("<I", data, offset)[0] ^ 0x4D10031E != item_id:
            raise ValueError("Item ID mismatch")
        type_code = struct.unpack_from("<I", data, offset + 8)[0]
        garage_type = struct.unpack_from("<I", data, offset + 20)[0]
        pos = offset + 55
        values = []
        for _ in range(4):
            if pos + 4 > end:
                raise ValueError("Missing string length")
            size = struct.unpack_from("<i", data, pos)[0]
            pos += 4
            if not 0 <= size <= 1024 * 1024 or pos + size > end:
                raise ValueError("Item string boundary")
            raw = bytes(v ^ ITEM_KEY[i % 8] for i, v in enumerate(data[pos:pos + size]))
            values.append(raw.decode("gb18030", errors="strict"))
            pos += size
        rows.append({"id": item_id, "typeCode": type_code, "garageType": garage_type,
                     "name": values[3], "recordOffset": offset})
    return version, sorted(rows, key=lambda r: r["id"])


def parse_shop_names(data):
    """Names by commodity ID only: a SKU ID must not become an inventory ID."""
    if len(data) < 16:
        raise ValueError("Truncated commoditylist")
    index = struct.unpack_from("<I", data, len(data) - 8)[0]
    if not 4 <= index <= len(data) - 12:
        raise ValueError("Invalid commodity index")
    count = struct.unpack_from("<I", data, index)[0]
    if count > 1_000_000 or index + 4 + count * 8 > len(data) - 8:
        raise ValueError("Invalid commodity index size")
    names, invalid = {}, []
    seen = set()
    for i in range(count):
        item_id, offset = struct.unpack_from("<II", data, index + 4 + i * 8)
        if item_id in seen or not 4 <= offset < index - 2:
            raise ValueError("Duplicate commodity ID or invalid offset")
        seen.add(item_id)
        size = struct.unpack_from("<H", data, offset)[0]
        if size < 8 or offset + 2 + size > index:
            raise ValueError("Commodity record boundary")
        raw = data[offset + 2:offset + 2 + size]
        head = bytes(v ^ SHOP_KEY[j % 8] for j, v in enumerate(raw[:8]))
        inner_size, inner_id, name_size = struct.unpack(">HIH", head)
        if (inner_size, inner_id) != (size, item_id) or name_size > size - 8:
            raise ValueError("Commodity header mismatch")
        name_raw = bytes(v ^ SHOP_KEY[j % 8] for j, v in enumerate(raw[8:8 + name_size]))
        try:
            name = name_raw.decode("gb18030", errors="strict")
            if not name or "\0" in name or "\ufffd" in name:
                raise UnicodeError("Invalid commodity name")
            names[item_id] = name
        except UnicodeError:
            invalid.append(item_id)
    return names, {"indexed": count, "invalid_name_ids": invalid,
                   "version": struct.unpack_from("<I", data)[0]}


def connect(ini_path):
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.read(ini_path, encoding="utf-8-sig")
    s = cfg["database_player"]
    endpoint = urllib.parse.urlsplit(s["host"] if "://" in s["host"] else "//" + s["host"])
    return pymysql.connect(
        host=endpoint.hostname, port=endpoint.port or 3306, user=s["user"],
        password=s["password"], database=s["database"], charset="utf8mb4",
        connect_timeout=6, read_timeout=60, write_timeout=60, autocommit=False,
        cursorclass=pymysql.cursors.DictCursor)


def fetch_rows(cursor, lock=False):
    cursor.execute("SELECT ID,Type,Mess,Name,BuyFlag,GameType,lockid "
                   "FROM ItemAllNew ORDER BY ID" + (" FOR UPDATE" if lock else ""))
    return list(cursor.fetchall())


def prepare(args):
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=False)
    data = Path(args.source).read_bytes()
    version, source_rows = parse_items(data)
    type_map = json.loads(Path(args.type_map).read_bytes())
    source_types = {str(r["typeCode"]) for r in source_rows}
    if source_types - type_map.keys():
        raise ValueError("Unmapped official type codes: " + str(sorted(source_types - type_map.keys())))
    shop_data = Path(args.shop).read_bytes()
    shop_names, shop_meta = parse_shop_names(shop_data)
    shop_inflated = zlib.decompress(Path(args.shop_spd).read_bytes())
    # Observed cache trailer differs only in its final four bytes; indexed data
    # and the preceding index-offset footer must match the compressed source.
    if shop_inflated[:-4] != shop_data[:-4]:
        raise ValueError("commoditylist.spd/save mismatch")
    with connect(args.ini) as db:
        with db.cursor() as q:
            q.execute("START TRANSACTION READ ONLY")
            q.execute("SHOW CREATE TABLE ItemAllNew")
            schema = q.fetchone()["Create Table"]
            if "ENGINE=InnoDB" not in schema:
                raise ValueError("Transaction requires InnoDB")
            before = fetch_rows(q)
        db.rollback()
    old = {r["ID"]: r for r in before}
    after = {k: dict(v) for k, v in old.items()}
    changes, catalog = [], []
    for item in source_rows:
        item_id = item["id"]
        rule = type_map[str(item["typeCode"])]
        item_type, mess = rule["type"], rule["mess"]
        # Lover-baby identities have a separate handler; do not create Pet rows.
        if item_id in BABY_IDS:
            if item["typeCode"] != 2328:
                raise ValueError("Lover baby source type changed")
            item_type, mess = "EAIT_BABY", "宠物"
        game_type = 2 if item["typeCode"] in (1560, 1561, 1562) else (
            1 if item["typeCode"] in (1550, 1551, 1552, 1553, 1554) else 0)
        name = shop_names.get(item_id, item["name"])
        name_source = "commoditylist" if item_id in shop_names else "itemlist"
        if not name and item_id in old:
            name = old[item_id]["Name"] or ""
            name_source = "existing_name_for_unnamed_official_record"
        # MySQL CHAR(255) strips trailing ASCII spaces on reads.
        name = name.rstrip(" ")
        if "\0" in name or "\ufffd" in name or len(name) > 255:
            raise ValueError("Invalid authoritative name for item " + str(item_id))
        row = dict(old[item_id]) if item_id in old else {
            "ID": item_id, "BuyFlag": 0, "lockid": 0}
        # The server removes spaces from Mess; do not rewrite equivalent labels.
        if item_id in old and (old[item_id]["Mess"] or "").replace(" ", "") == mess:
            mess = old[item_id]["Mess"]
        # Keep established finer labels (e.g. engagement ring within a clothing
        # type code) when the server class is already correct.
        if (item_id in old and old[item_id]["Type"] == item_type
                and old[item_id]["Mess"]
                and old[item_id]["Mess"] not in ("未知", "未知类型", "车辆", "服饰")):
            mess = old[item_id]["Mess"]
        if item_id in old and game_type == 0 and old[item_id]["GameType"] is None:
            game_type = None
        row.update(Type=item_type, Mess=mess, Name=name, GameType=game_type)
        after[item_id] = row
        catalog.append(dict(item, serverType=item_type, mess=mess, displayName=name,
                            nameSource=name_source))
        if old.get(item_id) != row:
            changes.append({"id": item_id, "before": old.get(item_id), "after": row})
    expected = [after[k] for k in sorted(after)]
    source_ids = {x["id"] for x in source_rows}
    unresolved = [r for r in before if r["ID"] not in source_ids]
    summary = {
        "source": str(Path(args.source).resolve()), "source_sha256": digest(data),
        "version": version, "source_rows": len(source_rows), "official_types": len(source_types),
        "shop": str(Path(args.shop).resolve()), "shop_sha256": digest(shop_data),
        "shop_meta": shop_meta, "database": "ItemAllNew",
        "before_rows": len(before), "after_rows": len(expected),
        "inserted": sum(c["before"] is None for c in changes),
        "updated": sum(c["before"] is not None for c in changes),
        "type_changed": sum(c["before"] is not None and c["before"]["Type"] != c["after"]["Type"]
                            for c in changes),
        "outside_source_preserved": len(unresolved),
        "source_type_counts": dict(collections.Counter(r["serverType"] for r in catalog)),
        "before_sha256": digest(packed(before)), "expected_sha256": digest(packed(expected)),
        "changes_sha256": digest(packed(changes)), "schema": schema,
        "type_map_sha256": digest(Path(args.type_map).read_bytes()),
    }
    for filename, value in [("before.json", before), ("expected.json", expected),
                            ("changes.json", changes), ("catalog.json", catalog),
                            ("outside-source-preserved.json", unresolved),
                            ("manifest.json", summary)]:
        write_json(output / filename, value)
    print(json.dumps({k: v for k, v in summary.items()
                      if k not in ("schema", "shop_meta")}, ensure_ascii=False))


def sql_value(value):
    if value is None:
        return "NULL"
    if isinstance(value, int):
        return str(value)
    return "CONVERT(X'" + str(value).encode("utf-8").hex() + "' USING utf8mb4)"


def backup_sql(path, rows):
    """Data-only recovery; deliberately no DROP, TRUNCATE, or DELETE."""
    with path.open("x", encoding="utf-8", newline="\n") as f:
        f.write("-- Full ItemAllNew snapshot. Does not remove later added IDs.\n")
        f.write("SET NAMES utf8mb4;\nSTART TRANSACTION;\n")
        for start in range(0, len(rows), 500):
            f.write("INSERT INTO ItemAllNew (ID,Type,Mess,Name,BuyFlag,GameType,lockid) VALUES\n")
            f.write(",\n".join("(" + ",".join(sql_value(r[k]) for k in FIELDS) + ")"
                              for r in rows[start:start + 500]))
            f.write("\nON DUPLICATE KEY UPDATE " +
                    ",".join(k + "=VALUES(" + k + ")" for k in FIELDS[1:]) + ";\n")
        f.write("COMMIT;\n")
        f.flush()
        os.fsync(f.fileno())


def apply_plan(args):
    p = Path(args.output)
    manifest = json.loads((p / "manifest.json").read_bytes())
    before = json.loads((p / "before.json").read_bytes())
    expected = json.loads((p / "expected.json").read_bytes())
    changes = json.loads((p / "changes.json").read_bytes())
    for value, key in [(before, "before_sha256"), (expected, "expected_sha256"),
                       (changes, "changes_sha256")]:
        if digest(packed(value)) != manifest[key]:
            raise ValueError("Prepared plan hash mismatch")
    if digest(Path(manifest["source"]).read_bytes()) != manifest["source_sha256"]:
        raise ValueError("Official source changed")
    if digest(Path(manifest["shop"]).read_bytes()) != manifest["shop_sha256"]:
        raise ValueError("Shop source changed")
    backup_sql(p / "ItemAllNew-before.sql", before)
    with connect(args.ini) as db:
        with db.cursor() as q:
            q.execute("SET SESSION innodb_lock_wait_timeout=15")
            q.execute("START TRANSACTION")
            current = fetch_rows(q, lock=True)
            if current != before:
                raise ValueError("Database changed since prepare; nothing applied")
            writes = [tuple(c["after"][k] for k in FIELDS) for c in changes]
            for start in range(0, len(writes), 500):
                q.executemany("INSERT INTO ItemAllNew (ID,Type,Mess,Name,BuyFlag,GameType,lockid) "
                              "VALUES (%s,%s,%s,%s,%s,%s,%s) ON DUPLICATE KEY UPDATE "
                              "Type=VALUES(Type),Mess=VALUES(Mess),Name=VALUES(Name),"
                              "GameType=VALUES(GameType)", writes[start:start + 500])
            actual = fetch_rows(q)
            if actual != expected:
                raise ValueError("Full transactional readback mismatch; rollback")
        db.commit()
        with db.cursor() as q:
            persisted = fetch_rows(q)
            if persisted != expected:
                raise ValueError("Post-commit readback mismatch; backup retained")
        db.rollback()
    result = {"committed": True, "rows": len(persisted),
              "sha256": digest(packed(persisted)), "inserted": manifest["inserted"],
              "updated": manifest["updated"], "type_changed": manifest["type_changed"],
              "all_rows_match": True, "buyflag_lockid_preserved": True}
    write_json(p / "result.json", result)
    print(json.dumps(result, ensure_ascii=False))


def rollback_plan(args):
    """Restore only this migration, refusing to overwrite any later change."""
    p = Path(args.output)
    manifest = json.loads((p / "manifest.json").read_bytes())
    before = json.loads((p / "before.json").read_bytes())
    expected = json.loads((p / "expected.json").read_bytes())
    changes = json.loads((p / "changes.json").read_bytes())
    for value, key in [(before, "before_sha256"), (expected, "expected_sha256"),
                       (changes, "changes_sha256")]:
        if digest(packed(value)) != manifest[key]:
            raise ValueError("Prepared rollback hash mismatch")
    with connect(args.ini) as db:
        with db.cursor() as q:
            q.execute("SET SESSION innodb_lock_wait_timeout=15")
            q.execute("START TRANSACTION")
            if fetch_rows(q, lock=True) != expected:
                raise ValueError("Database changed since migration; rollback refused")
            for change in changes:
                row = change["before"]
                if row is None:
                    q.execute("DELETE FROM ItemAllNew WHERE ID=%s", (change["id"],))
                else:
                    q.execute("UPDATE ItemAllNew SET Type=%s,Mess=%s,Name=%s,"
                              "BuyFlag=%s,GameType=%s,lockid=%s WHERE ID=%s",
                              tuple(row[k] for k in (*FIELDS[1:], "ID")))
            if fetch_rows(q) != before:
                raise ValueError("Rollback readback mismatch")
        db.commit()
    print(json.dumps({"rolled_back": True, "rows": len(before)}))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("mode", choices=("prepare", "apply", "rollback"))
    ap.add_argument("--ini", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--source")
    ap.add_argument("--shop")
    ap.add_argument("--shop-spd")
    ap.add_argument("--type-map")
    args = ap.parse_args()
    if args.mode == "prepare" and not all((args.source, args.shop, args.shop_spd, args.type_map)):
        ap.error("prepare requires source, shop, shop-spd, and type-map")
    try:
        {"prepare": prepare, "apply": apply_plan, "rollback": rollback_plan}[args.mode](args)
    except pymysql.MySQLError as e:
        print("MySQL operation failed; code=" + str(e.args[0] if e.args else "unknown"),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
