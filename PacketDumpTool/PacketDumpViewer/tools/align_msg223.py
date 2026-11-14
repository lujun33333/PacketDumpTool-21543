#!/usr/bin/env python3
"""MsgID 223 fixed-full layout evidence scanner.

Scans PacketDumpTool payloads and writes a compact evidence report for
CSSafeCNotifyLoginMoreInfoPara expanded full payloads (current 21273 layout).
"""

from __future__ import annotations

import collections
import hashlib
import json
import re
import struct
from pathlib import Path


DEFAULT_DUMP_ROOT = Path(r"F:\PacketDumpTool\PacketDumpTool\x64\Release")
REPORT_JSON = Path(r"F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\docs\msg223_alignment_report.json")
REPORT_MD = Path(r"F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\docs\msg223_alignment_report.md")

FULL_LEN = 700_668
BODY_SLICE_LEN = 700_654

ITEM_NUM = 0x00000000
ITEM_ARRAY = 0x00000002
ITEM_CAP = 8050
ITEM_STRIDE = 23
TASK_NUM = ITEM_ARRAY + ITEM_CAP * ITEM_STRIDE
TASK_ARRAY = TASK_NUM + 2
TASK_CAP = 1100
TASK_STRIDE = 24
PET_NUM = TASK_ARRAY + TASK_CAP * TASK_STRIDE
PET_ARRAY = PET_NUM + 2
PET_CAP = 500
PET_STRIDE = 191
MAP_NUM = PET_ARRAY + PET_CAP * PET_STRIDE
MAP_ARRAY = MAP_NUM + 2
MAP_CAP = 500
MAP_STRIDE = 12
OVERDUE_NUM = MAP_ARRAY + MAP_CAP * MAP_STRIDE
OVERDUE_ARRAY = OVERDUE_NUM + 1
OVERDUE_CAP = 100
OVERDUE_STRIDE = 12
STONE_NUM = OVERDUE_ARRAY + OVERDUE_CAP * OVERDUE_STRIDE
STONE_ARRAY = STONE_NUM + 4
STONE_CAP = 8050
STONE_STRIDE = 48
HAS_NEXT = STONE_ARRAY + STONE_CAP * STONE_STRIDE
CHECK_FIELD = HAS_NEXT + 1


FILENAME_RE = re.compile(
    r"network_bin_(?P<pid>\d+)_(?P<seq>\d+)_(?P<direction>.+)_msg(?P<msgid>\d+)_len(?P<len>\d+)\.bin$"
)


def u8(data: bytes, offset: int) -> int:
    return data[offset]


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_name(path: Path) -> dict[str, str | int] | None:
    m = FILENAME_RE.match(path.name)
    if not m:
        return None
    d: dict[str, str | int] = m.groupdict()
    d["pid"] = int(d["pid"])
    d["seq"] = int(d["seq"])
    d["msgid"] = int(d["msgid"])
    d["len"] = int(d["len"])
    return d


def scan(root: Path) -> dict:
    all_223 = []
    for path in sorted(root.glob(r"packet_dumps_*\payloads\*msg223*.bin")):
        meta = parse_name(path)
        if not meta:
            continue
        all_223.append((path, meta))

    fulls = [(p, m) for p, m in all_223 if int(m["len"]) == FULL_LEN and str(m["direction"]).endswith("_full")]
    recv_fulls = [(p, m) for p, m in fulls if str(m["direction"]) == "recv_plain_full"]
    body_slices = [(p, m) for p, m in all_223 if int(m["len"]) == BODY_SLICE_LEN and str(m["direction"]).endswith("_body")]

    body_hashes = collections.defaultdict(list)
    for path, meta in body_slices:
        body_hashes[sha256(path.read_bytes())].append(str(path))

    samples = []
    counters = {k: collections.Counter() for k in ["item_num", "task_num", "pet_num", "map_num", "overdue_num", "stone_num", "has_next", "check_field"]}
    truncation_matches = 0
    seen_full_hashes = set()
    for path, meta in recv_fulls:
        data = path.read_bytes()
        if len(data) != FULL_LEN:
            continue
        full_hash = sha256(data)
        if full_hash in seen_full_hashes:
            continue
        seen_full_hashes.add(full_hash)

        values = {
            "item_num": u16(data, ITEM_NUM),
            "task_num": u16(data, TASK_NUM),
            "pet_num": u16(data, PET_NUM),
            "map_num": u16(data, MAP_NUM),
            "overdue_num": u8(data, OVERDUE_NUM),
            "stone_num": u32(data, STONE_NUM),
            "has_next": u8(data, HAS_NEXT),
            "check_field": f"0x{u32(data, CHECK_FIELD):08X}",
        }
        for k, v in values.items():
            counters[k][v] += 1

        body_hash = sha256(data[14:])
        has_body_match = body_hash in body_hashes
        truncation_matches += int(has_body_match)
        samples.append(
            {
                "file": str(path),
                "pid": meta["pid"],
                "seq": meta["seq"],
                **values,
                "full_sha256": full_hash,
                "full_14_to_end_matches_body_slice": has_body_match,
                "first_items": [
                    {
                        "index": i,
                        "offset": f"0x{ITEM_ARRAY + i * ITEM_STRIDE:08X}",
                        "item_id": u32(data, ITEM_ARRAY + i * ITEM_STRIDE),
                        "item_num": u32(data, ITEM_ARRAY + i * ITEM_STRIDE + 4),
                        "avail_period": u32(data, ITEM_ARRAY + i * ITEM_STRIDE + 8),
                        "status": u8(data, ITEM_ARRAY + i * ITEM_STRIDE + 12),
                        "obtain_time": u32(data, ITEM_ARRAY + i * ITEM_STRIDE + 13),
                        "other_attribute": u32(data, ITEM_ARRAY + i * ITEM_STRIDE + 17),
                        "item_type": u16(data, ITEM_ARRAY + i * ITEM_STRIDE + 21),
                    }
                    for i in range(min(3, values["item_num"]))
                ],
            }
        )

    layout = [
        ("m_ushItemNum", ITEM_NUM, 2, "le:u16", "active item count"),
        ("m_astPlayerDBItemInfo[]", ITEM_ARRAY, ITEM_CAP * ITEM_STRIDE, f"array[{ITEM_CAP}] stride={ITEM_STRIDE}", "fixed item slots"),
        ("m_ushTaskNum", TASK_NUM, 2, "le:u16", "active task count"),
        ("m_astPlayerDBTaskInfo[]", TASK_ARRAY, TASK_CAP * TASK_STRIDE, f"array[{TASK_CAP}] stride={TASK_STRIDE}", "fixed task slots"),
        ("m_usPetNum", PET_NUM, 2, "le:u16", "active pet count"),
        ("m_astPetInfo[]", PET_ARRAY, PET_CAP * PET_STRIDE, f"array[{PET_CAP}] stride={PET_STRIDE}", "fixed pet slots"),
        ("m_usMapNum", MAP_NUM, 2, "le:u16", "active map count"),
        ("m_astRecord[]", MAP_ARRAY, MAP_CAP * MAP_STRIDE, f"array[{MAP_CAP}] stride={MAP_STRIDE}", "fixed map records"),
        ("m_uchOverdueItemNum", OVERDUE_NUM, 1, "u8", "active overdue item count"),
        ("m_astOverdueItem[]", OVERDUE_ARRAY, OVERDUE_CAP * OVERDUE_STRIDE, f"array[{OVERDUE_CAP}] stride={OVERDUE_STRIDE}", "fixed overdue item slots"),
        ("m_iStoneKartNum", STONE_NUM, 4, "le:u32", "active stone-kart count"),
        ("m_astKartStoneGrooveInfo[]", STONE_ARRAY, STONE_CAP * STONE_STRIDE, f"array[{STONE_CAP}] stride={STONE_STRIDE}", "fixed stone kart slots"),
        ("m_bHasNext", HAS_NEXT, 1, "u8", "more chunks flag"),
        ("DecodeCheckField", CHECK_FIELD, 4, "le:u32", "special28346 decode check field; semantic not business"),
    ]

    return {
        "dump_root": str(root),
        "all_msg223_payloads": len(all_223),
        "recv_plain_full_samples": len(recv_fulls),
        "unique_recv_plain_full_samples": len(samples),
        "body_slice_samples": len(body_slices),
        "full_14_to_end_body_slice_matches": truncation_matches,
        "layout": [
            {"name": n, "offset": f"0x{o:08X}", "size": s, "type": t, "note": note}
            for n, o, s, t, note in layout
        ],
        "counters": {k: dict(v) for k, v in counters.items()},
        "samples": samples,
    }


def write_reports(report: dict) -> None:
    REPORT_JSON.parent.mkdir(parents=True, exist_ok=True)
    REPORT_JSON.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    lines = [
        "# MsgID 223 / CSSafeCNotifyLoginMoreInfoPara alignment report",
        "",
        f"- dump root: `{report['dump_root']}`",
        f"- all msg223 payloads: `{report['all_msg223_payloads']}`",
        f"- recv_plain_full samples: `{report['recv_plain_full_samples']}`",
        f"- unique recv_plain_full samples: `{report['unique_recv_plain_full_samples']}`",
        f"- body slice samples: `{report['body_slice_samples']}`",
        f"- full[0x0E:] matched body slice samples: `{report['full_14_to_end_body_slice_matches']}`",
        "",
        "## Fixed expanded layout",
        "",
        "| Name | Offset | Size | Type | Note |",
        "|---|---:|---:|---|---|",
    ]
    for row in report["layout"]:
        lines.append(f"| `{row['name']}` | `{row['offset']}` | `{row['size']}` | `{row['type']}` | {row['note']} |")

    lines += ["", "## Count distributions", ""]
    for name, counter in report["counters"].items():
        lines.append(f"- `{name}`: `{counter}`")

    lines += [
        "",
        "## Evidence notes",
        "",
        "- `recv_plain_full` / `special28346_after_full` length `700668` is the authoritative expanded full layout.",
        "- `recv_plain_body` / `special28346_after_body` length `700654` equals `full[0x0E:]`; for Msg223 special decode this is a truncated view, not the structure start.",
        "- Field order and fixed offsets are backed by `F:\\new1\\dll\\new\\21273\\Network.dll.i64` export `sub_18032F740`; old server comments provide the task/map/overdue/stone field names.",
        "- Unknown semantic tail fields are intentionally marked `UnknownName`; no names are invented from zero values.",
    ]
    REPORT_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    report = scan(DEFAULT_DUMP_ROOT)
    write_reports(report)
    print(f"wrote {REPORT_JSON}")
    print(f"wrote {REPORT_MD}")


if __name__ == "__main__":
    main()
