from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import re
import struct
import sys
from dataclasses import asdict, dataclass
from pathlib import Path


FULL_LEN = 317_329
BODY_LEN = 317_315
FULL_TO_BODY_DELTA = 0x0E

ITEM_NUM = 0x156
ITEM_ARRAY = 0x158
ITEM_STRIDE = 23
RECOM_COUNTRY = 0x33BB8
MAP_NUM = 0x33D8C
MAP_ARRAY = 0x33D8E
MAP_STRIDE = 12
PET_NUM = 0x354FE
PET_ARRAY = 0x35500
PET_STRIDE = 191
TAIL = 0x4CA0C
OVERDUE_NUM = 0x4CA2E
HAS_MORE_INFO = 0x4CF20
CUR_SVR_ID = 0x4CF21
EXTEND_ITEM_NUM = 0x4CF30
TASK_COUNT_OBJECT_OFFSET = 0x310328
TASK_ARRAY_OBJECT_OFFSET = 0x310330
TASK_SAFE_OBJECT_STRIDE = 320
TASK_SAFE_OBJECT_CAPACITY = 1100
TASK_WIRE_RECORD_STRIDE = 24

REPO_ROOT_FOR_DECODER = Path(r"F:\new1")

UNRESOLVED_FULL_OFFSETS = [
    {
        "range": "0x00033BBE-0x00033D7B",
        "name": "m_stPlayerRelationInfo expanded / padding",
        "status": "UnknownName",
        "reason": "Network.dll 有 m_stPlayerRelationInfo.* 字段名，但 full 展开区是固定占位；当前 dump 非零 run 太少，不能把每个成员提升成 full offset。",
    },
    {
        "range": "0x00033D7C-0x00033D8B",
        "name": "PreMapUnknownU32_1..4",
        "status": "UnknownName",
        "reason": "反编译顺序显示在 map/record array 前有 4 个 u32；未找到能证明语义的 21273 字符串/消费点。",
    },
    {
        "range": "0x00033D8C-0x00033D8D",
        "name": "m_astRecord count",
        "status": "UnknownName",
        "reason": "后续 m_astRecord[] stride=12 已由多 dump 验证；count 字段自身未找到直接官方成员名。",
    },
    {
        "range": "m_astPetInfo[i]+0x20/+0x24/+0x28/+0x2D/+0x2F",
        "name": "pet level/PL fields",
        "status": "UnknownName",
        "reason": "offset/value 规律稳定，但 21273 字符串没有对应直接成员名；保留描述名，不伪造成 m_astPetInfo 官方路径。",
    },
    {
        "range": "m_astPetInfo[i]+0xBA-0xBE",
        "name": "m_astPetInfo[i].CommonTailUnknown",
        "status": "UnknownName",
        "reason": "Network.dll 有 stCommonInfo/PKStar/PetSkinID/RoleID 字符串，但当前 5 字节尾部不能同时容纳并证明这些字段。",
    },
    {
        "range": "0x0004CA2E",
        "name": "m_astOverdueItem count",
        "status": "UnknownName",
        "reason": "m_astOverdueItem[] 三个成员已确认；count 名称未找到直接字符串。",
    },
    {
        "range": "0x0004CA43-0x0004CF1F",
        "name": "PreFilterHasMoreInfo.RawBlock",
        "status": "UnknownName/Reserved",
        "reason": "该区在 full 中为模板/保留混合区；多 dump 有非零 run，但未找到稳定消费点可命名。",
    },
    {
        "range": "0x0004D022-0x0004D02D",
        "name": "TailU32_1..3",
        "status": "UnknownName",
        "reason": "位于 TssTicketLen 后、FinalFixedArea 前；反编译字段表仍是通用 m_uiPara/tail 形态，未找到官方成员名。",
    },
    {
        "range": "0x0004D02E-0x0004D790",
        "name": "PostExtendTail.FinalFixedArea",
        "status": "UnknownName/Reserved",
        "reason": "full 尾部固定 1891 bytes；当前 dump 内存在非零 run，不能整体标 Reserved，也不能无证据拆字段。",
    },
    {
        "range": "before_inner obj+0x310328 / obj+0x310330",
        "name": "m_astPlayerDBTaskInfo[]",
        "status": "VerifiedEmpty + Template",
        "reason": "当前所有可解码 before_inner 的 taskNum=0，官服表达为空数组；字段名/记录顺序/对象 offset 已确认，非零记录的 317329 full offset 仍需 taskNum>0 样本。",
    },
]

COUNTED_ARRAY_POLICIES = [
    {
        "array": "m_astPlayerDBItemInfo[]",
        "count": "m_iItemNum",
        "layout": "fixed full slots",
        "zero_fill": "active_records=0; ReservedSlots/default area remains",
    },
    {
        "array": "m_astRecord[]",
        "count": "m_astRecordCount (count name not official)",
        "layout": "fixed full slots",
        "zero_fill": "active_records=0; ReservedSlots/default area remains",
    },
    {
        "array": "m_astPetInfo[]",
        "count": "m_usPetNum",
        "layout": "fixed full slots",
        "zero_fill": "active_records=0; ReservedSlots/default area remains",
    },
    {
        "array": "m_astPetInfo[i].m_astPetSkillList[]",
        "count": "m_usPetSkillNum",
        "layout": "fixed 3 slots inside each pet record",
        "zero_fill": "active_skills=0; 3 skill slots are inactive/default",
    },
    {
        "array": "m_astOverdueItem[]",
        "count": "m_uchOverdueItemNum (count name not official)",
        "layout": "fixed full slots",
        "zero_fill": "active_records=0; ReservedSlots/default area remains",
    },
    {
        "array": "m_astPlayerDBTaskInfo[]",
        "count": "m_shTaskNum / obj+0x310328",
        "layout": "dynamic before_inner object array; full nonzero offset not proven",
        "zero_fill": "empty array only; do not synthesize element[0]",
    },
]

FILENAME_RE = re.compile(
    r"network_bin_(?P<pid>\d+)_(?P<seq>\d+)_(?P<direction>.+)_msg(?P<msgid>\d+)_len(?P<length>\d+)\.bin$",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class PayloadRecord:
    folder: str
    pid: int
    seq: int
    direction: str
    declared_len: int
    actual_len: int
    category: str
    sha256_16: str
    path: str


def u8(data: bytes, offset: int) -> int:
    return data[offset] if 0 <= offset < len(data) else 0


def u16(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little") if 0 <= offset + 2 <= len(data) else 0


def u32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little") if 0 <= offset + 4 <= len(data) else 0


def category(direction: str, length: int) -> str:
    d = direction.lower()
    if length == FULL_LEN and ("special28346_after_full" in d or "recv_plain_full" in d):
        return "trusted_full"
    if length == BODY_LEN and ("special28346_after_body" in d or "recv_plain_body" in d):
        return "trusted_body_full_minus_0x0e"
    if length == FULL_LEN and "login_cache_decode_after" in d:
        return "cache_after_full_view"
    if length == FULL_LEN and "login_cache_decode_before" in d:
        return "cache_before_not_raw_layout"
    if "special28346_before_inner_body_at12" in d:
        return "before_inner_encoded_or_safe_layout"
    if "serialize_body_plain" in d:
        return "client_request_serialize_body"
    return "other_msg100"


def iter_records(payload_root: Path) -> list[PayloadRecord]:
    records: list[PayloadRecord] = []
    for path in sorted(payload_root.glob("packet_dumps_*\\payloads\\*.bin")):
        match = FILENAME_RE.match(path.name)
        if not match or int(match["msgid"]) != 100:
            continue

        data = path.read_bytes()
        direction = match["direction"]
        declared_len = int(match["length"])
        records.append(
            PayloadRecord(
                folder=path.parents[1].name,
                pid=int(match["pid"]),
                seq=int(match["seq"]),
                direction=direction,
                declared_len=declared_len,
                actual_len=len(data),
                category=category(direction, declared_len),
                sha256_16=hashlib.sha256(data).hexdigest()[:16],
                path=str(path),
            )
        )
    return records


def extract_network_names(network_dll: Path) -> dict[str, list[str]]:
    data = network_dll.read_bytes()
    wanted = {
        "response_login_class": rb"CSSafeCResponseLoginPara[A-Za-z0-9_:<>~]*",
        "task": rb"m_astPlayerDBTaskInfo\[%d\]\.[A-Za-z0-9_]+:%[dux]",
        "item": rb"m_astPlayerDBItemInfo\[%d\]\.[A-Za-z0-9_]+:%[dux]",
        "base": rb"m_stPlayerDBBaseInfo\.[A-Za-z0-9_\[\]%]+:?%?[dux]*",
        "relation": rb"m_stPlayerRelationInfo\.[A-Za-z0-9_\[\]%]+:?%?[a-z]*",
        "record": rb"m_astRecord\[[%dT12]+\]\.[A-Za-z0-9_\[\]%]+:?%?[dux]*",
        "pet": rb"m_astPetInfo\[%d\]\.[A-Za-z0-9_\[\]%\.]+:?%?[dux]*",
        "overdue": rb"m_astOverdueItem\[%d\]\.[A-Za-z0-9_]+:%[dux]",
        "noble": rb"m_stNobleInfo\.[A-Za-z0-9_]+:%[dux]",
        "guild_vip": rb"m_stGuildVipBaseInfo\.[A-Za-z0-9_]+:%[dux]",
        "ranked": rb"m_stRankedMatchGradeInfo(?:\[%d\])?\.[A-Za-z0-9_]+:%[dux]",
    }
    result: dict[str, list[str]] = {}
    for key, pattern in wanted.items():
        values = []
        for match in re.finditer(pattern, data):
            text = match.group(0).decode("ascii", errors="ignore")
            if text not in values:
                values.append(text)
        result[key] = values
    return result


def _counter_dict(values) -> dict[str, int]:
    return {str(key): value for key, value in collections.Counter(values).items()}


def _safe_tuple_count(value) -> int | None:
    if isinstance(value, tuple) and value:
        first = value[0]
        return first if isinstance(first, int) else None
    return None


def decode_before_inner_task_summary(records: list[PayloadRecord]) -> dict[str, object]:
    """Decode special28346_before_inner MsgID100 samples with the local loginpara decoder.

    This intentionally keeps the evidence separate from full-offset parsing:
    before_inner is CSSafe wire/object-space evidence, while 317329 full files
    are the expanded viewer layout.
    """

    if str(REPO_ROOT_FOR_DECODER) not in sys.path and REPO_ROOT_FOR_DECODER.exists():
        sys.path.insert(0, str(REPO_ROOT_FOR_DECODER))

    try:
        from qqspeed_decoder.loginpara import deserialize_loginpara
    except Exception as exc:  # pragma: no cover - diagnostic script fallback
        return {
            "status": "decoder_unavailable",
            "error": repr(exc),
            "samples": [],
        }

    rows = []
    for record in records:
        if record.category != "before_inner_encoded_or_safe_layout":
            continue

        data = Path(record.path).read_bytes()
        try:
            decoded = deserialize_loginpara(data, strict=False)
            obj = decoded.obj
            task_num = obj.get("varint_0x310328")
            task_array_count = _safe_tuple_count(obj.get("subobj_array_0x310330"))
            overdue_count = _safe_tuple_count(obj.get("subobj_array_0x30bbe0"))
            row = {
                **asdict(record),
                "decoder_status": "ok",
                "cursor_pos": decoded.cursor_pos,
                "fields_consumed": decoded.fields_consumed,
                "truncated": bool(decoded.truncated),
                "truncation_reason": decoded.truncation_reason,
                "item_num": obj.get("varint_0xd0a"),
                "extend_item_num": obj.get("varint_0x16a702"),
                "pet_num": obj.get("varint_0x16aa0c"),
                "map_num": obj.get("varint_at_0x2F685E"),
                "overdue_count": overdue_count,
                "task_count_object_offset": f"0x{TASK_COUNT_OBJECT_OFFSET:X}",
                "task_array_object_offset": f"0x{TASK_ARRAY_OBJECT_OFFSET:X}",
                "task_num": task_num,
                "task_array_count": task_array_count,
            }
        except Exception as exc:  # pragma: no cover - diagnostic script fallback
            row = {
                **asdict(record),
                "decoder_status": "error",
                "error": repr(exc),
            }
        rows.append(row)

    task_values = [row.get("task_num") for row in rows]
    task_array_values = [row.get("task_array_count") for row in rows]
    usable_task_values = [value for value in task_values if isinstance(value, int)]
    return {
        "status": "ok",
        "count": len(rows),
        "decoded_ok": sum(1 for row in rows if row.get("decoder_status") == "ok"),
        "fully_consumed": sum(
            1
            for row in rows
            if row.get("decoder_status") == "ok" and row.get("cursor_pos") == row.get("actual_len")
        ),
        "task_num_counter": _counter_dict(task_values),
        "task_array_count_counter": _counter_dict(task_array_values),
        "task_num_all_decodable_zero": bool(usable_task_values) and all(value == 0 for value in usable_task_values),
        "rows": rows,
        "object_layout": {
            "task_count_obj_offset": f"0x{TASK_COUNT_OBJECT_OFFSET:X}",
            "task_array_obj_offset": f"0x{TASK_ARRAY_OBJECT_OFFSET:X}",
            "safe_object_stride": TASK_SAFE_OBJECT_STRIDE,
            "safe_object_capacity": TASK_SAFE_OBJECT_CAPACITY,
            "wire_record_stride": TASK_WIRE_RECORD_STRIDE,
            "wire_record_fields": [
                {"offset": 0, "size": 4, "type": "le:i32", "name": "m_astPlayerDBTaskInfo[i].m_iTaskID"},
                {"offset": 4, "size": 2, "type": "le:i16", "name": "m_astPlayerDBTaskInfo[i].m_shOptionID"},
                {"offset": 6, "size": 1, "type": "i8", "name": "m_astPlayerDBTaskInfo[i].m_cStatus"},
                {"offset": 7, "size": 1, "type": "u8", "name": "m_astPlayerDBTaskInfo[i].m_bPrevStatus"},
                {"offset": 8, "size": 4, "type": "le:i32", "name": "m_astPlayerDBTaskInfo[i].m_iProgressRate"},
                {"offset": 12, "size": 4, "type": "le:i32", "name": "m_astPlayerDBTaskInfo[i].m_iOther"},
                {"offset": 16, "size": 4, "type": "le:u32", "name": "m_astPlayerDBTaskInfo[i].m_uiTakeTime"},
                {"offset": 20, "size": 4, "type": "le:u32", "name": "m_astPlayerDBTaskInfo[i].m_uiLastUpdateTime"},
            ],
        },
    }


def _parse_pe_text_section(network_dll: Path) -> tuple[int, bytes, int]:
    data = network_dll.read_bytes()
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew : e_lfanew + 4] != b"PE\0\0":
        raise ValueError(f"not a PE file: {network_dll}")
    coff = e_lfanew + 4
    section_count = struct.unpack_from("<H", data, coff + 2)[0]
    optional_size = struct.unpack_from("<H", data, coff + 16)[0]
    optional = coff + 20
    image_base = struct.unpack_from("<Q", data, optional + 24)[0]
    section_table = optional + optional_size
    for index in range(section_count):
        offset = section_table + index * 40
        name = data[offset : offset + 8].rstrip(b"\0").decode("ascii", errors="ignore")
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", data, offset + 8)
        if name == ".text":
            return image_base, data[raw_pointer : raw_pointer + raw_size], virtual_address
    raise ValueError(".text section not found")


def extract_ida_task_evidence(ida_export: Path, network_dll: Path) -> dict[str, object]:
    strings_csv = ida_export / "strings_redacted.csv"
    functions_csv = ida_export / "functions.csv"
    if not strings_csv.exists() or not functions_csv.exists() or not network_dll.exists():
        return {
            "status": "missing",
            "ida_export": str(ida_export),
            "network_dll": str(network_dll),
        }

    task_strings: dict[int, str] = {}
    with strings_csv.open("r", encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            preview = row.get("preview", "")
            if "m_astPlayerDBTaskInfo" in preview or "m_shTaskNum" in preview or "CNotifyAddTaskInfoPara" in preview:
                task_strings[int(row["ea"], 16)] = preview

    functions = []
    with functions_csv.open("r", encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            functions.append(
                {
                    "name": row["name"],
                    "rva": int(row["rva"], 16),
                    "end_rva": int(row["end_rva"], 16),
                    "pseudoc_file": row["pseudoc_file"],
                }
            )

    def find_function(rva: int) -> dict[str, object] | None:
        for function in functions:
            if function["rva"] <= rva < function["end_rva"]:
                return function
        return None

    try:
        image_base, text_bytes, text_rva = _parse_pe_text_section(network_dll)
    except Exception as exc:  # pragma: no cover - diagnostic script fallback
        return {"status": "pe_scan_error", "error": repr(exc)}

    xrefs = []
    for index in range(0, len(text_bytes) - 4):
        displacement = struct.unpack_from("<i", text_bytes, index)[0]
        target = image_base + text_rva + index + 4 + displacement
        preview = task_strings.get(target)
        if preview is None:
            continue
        ref_rva = text_rva + index
        function = find_function(ref_rva)
        xrefs.append(
            {
                "ref_rva": f"0x{ref_rva:X}",
                "string": preview,
                "function": function["name"] if function else None,
                "function_rva": f"0x{function['rva']:X}" if function else None,
                "pseudoc_file": function["pseudoc_file"] if function else None,
            }
        )

    return {
        "status": "ok",
        "task_string_count": len(task_strings),
        "task_string_xrefs": xrefs,
        "response_login_task_array_evidence": [
            {
                "source": r"pseudoc\00306DC0_sub_180306DC0.c",
                "evidence": "CSSafeCResponseLoginPara ctor calls sub_18040DE28(a1 + 3212080, 320, 1100, sub_180318EE0, sub_180321760)",
                "meaning": "obj+0x310330 = CSSafeSTPlayerDBTaskInfo array, safe-object stride 320, capacity 1100",
            },
            {
                "source": r"pseudoc\0031FED0_sub_18031FED0.c",
                "evidence": "CSSafeCResponseLoginPara cleanup calls sub_18040DD58(a1 + 3212080, 320, 1100, sub_180321760)",
                "meaning": "same object offset/stride is present in destructor cleanup",
            },
            {
                "source": r"pseudoc\000D2CB0_sub_1800D2CB0.c + Network.dll disassembly",
                "evidence": "CNotifyAddTaskInfoPara prints m_shTaskNum and eight m_astPlayerDBTaskInfo fields from a 24-byte record loop",
                "meaning": "wire/debug task record order is known; full expanded offset still needs a non-zero Msg100 task sample",
            },
        ],
    }


def parse_full_anchors(path: Path) -> dict[str, int | str]:
    data = path.read_bytes()
    item_num = u16(data, ITEM_NUM)
    map_num = u16(data, MAP_NUM)
    pet_num = u16(data, PET_NUM)
    overdue_num = u8(data, OVERDUE_NUM)
    return {
        "len": len(data),
        "result_id": u16(data, 0),
        "key_len": u8(data, 2),
        "uin": u32(data, 0x23),
        "identity": u32(data, 0x27),
        "conn_id": u16(data, 0x2B),
        "game_svr_identity": u32(data, 0x31),
        "item_num": item_num,
        "item_capacity": max(0, (RECOM_COUNTRY - ITEM_ARRAY) // ITEM_STRIDE),
        "map_num": map_num,
        "map_capacity": max(0, (PET_NUM - MAP_ARRAY) // MAP_STRIDE),
        "pet_num": pet_num,
        "pet_capacity": max(0, (TAIL - PET_ARRAY) // PET_STRIDE),
        "overdue_num": overdue_num,
        "has_more_info": u8(data, HAS_MORE_INFO),
        "cur_svr_id": u16(data, CUR_SVR_ID),
        "extend_item_num": u16(data, EXTEND_ITEM_NUM),
        "layout_ok": int(
            len(data) == FULL_LEN
            and item_num <= max(0, (RECOM_COUNTRY - ITEM_ARRAY) // ITEM_STRIDE)
            and map_num <= max(0, (PET_NUM - MAP_ARRAY) // MAP_STRIDE)
            and pet_num <= max(0, (TAIL - PET_ARRAY) // PET_STRIDE)
        ),
    }


def compare_bodies(records: list[PayloadRecord]) -> dict[str, int]:
    full_by_folder: dict[str, list[bytes]] = collections.defaultdict(list)
    for record in records:
        if record.category != "trusted_full":
            continue
        full_by_folder[record.folder].append(Path(record.path).read_bytes())

    total = 0
    matched = 0
    for record in records:
        if record.category != "trusted_body_full_minus_0x0e":
            continue
        total += 1
        body = Path(record.path).read_bytes()
        if any(full[FULL_TO_BODY_DELTA:] == body for full in full_by_folder.get(record.folder, [])):
            matched += 1
    return {"body_total": total, "body_matches_full_minus_0x0e": matched, "body_unmatched": total - matched}


def summarize_anchor_sets(records: list[PayloadRecord], include_categories: set[str]) -> dict[str, object]:
    rows = []
    for record in records:
        if record.category not in include_categories:
            continue
        anchors = parse_full_anchors(Path(record.path))
        rows.append({**asdict(record), **anchors})

    counters = {
        "uin": collections.Counter(row["uin"] for row in rows),
        "item_num": collections.Counter(row["item_num"] for row in rows),
        "map_num": collections.Counter(row["map_num"] for row in rows),
        "pet_num": collections.Counter(row["pet_num"] for row in rows),
        "extend_item_num": collections.Counter(row["extend_item_num"] for row in rows),
        "layout_ok": collections.Counter(row["layout_ok"] for row in rows),
    }
    return {
        "count": len(rows),
        "unique_anchor_tuples": len(
            {
                (
                    row["uin"],
                    row["item_num"],
                    row["map_num"],
                    row["pet_num"],
                    row["extend_item_num"],
                )
                for row in rows
            }
        ),
        "counters": {name: dict(counter) for name, counter in counters.items()},
        "rows": rows,
    }


def write_markdown(
    report_path: Path,
    payload_root: Path,
    network_dll: Path,
    records: list[PayloadRecord],
    names: dict[str, list[str]],
    trusted_summary: dict[str, object],
    cache_after_summary: dict[str, object],
    body_compare: dict[str, int],
    before_inner_task_summary: dict[str, object],
    ida_task_evidence: dict[str, object],
) -> None:
    by_category = collections.Counter(record.category for record in records)
    by_len = collections.Counter(record.declared_len for record in records)
    by_folder = collections.Counter(record.folder for record in records)

    def table_counter(counter: collections.Counter | dict) -> str:
        return "\n".join(f"| `{key}` | {value} |" for key, value in sorted(counter.items(), key=lambda kv: str(kv[0])))

    name_lines = []
    for group, values in names.items():
        preview = "<br>".join(f"`{value}`" for value in values[:16])
        more = "" if len(values) <= 16 else f"<br>... +{len(values) - 16}"
        name_lines.append(f"| `{group}` | {len(values)} | {preview}{more} |")

    task_obj = before_inner_task_summary.get("object_layout") or {}
    task_fields = task_obj.get("wire_record_fields") or []
    task_field_lines = []
    for field in task_fields:
        task_field_lines.append(
            f"| `+0x{field['offset']:02X}` | {field['size']} | `{field['type']}` | `{field['name']}` |"
        )

    xrefs = ida_task_evidence.get("task_string_xrefs") or []
    xref_lines = []
    for xref in xrefs[:16]:
        xref_lines.append(
            f"| `{xref.get('ref_rva')}` | `{xref.get('function')}` | `{xref.get('function_rva')}` | `{xref.get('string')}` |"
        )

    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(
        "\n".join(
            [
                "# MsgID 100 全量对齐报告",
                "",
                "## 输入证据",
                "",
                f"- payload 根目录：`{payload_root}`",
                f"- 最新字段名来源：`{network_dll}`",
                "- offset/value 来源：`packet_dumps_*` 中的 MsgID 100 payload",
                "- full 标准：`317329 bytes`；body 标准：`317315 bytes = full[0x0E:]`",
                "",
                "## 样本分类",
                "",
                "| category | count |",
                "|---|---:|",
                table_counter(by_category),
                "",
                "## 长度分布",
                "",
                "| declared length | count |",
                "|---:|---:|",
                table_counter(by_len),
                "",
                "## dump 目录分布",
                "",
                "| folder | count |",
                "|---|---:|",
                table_counter(by_folder),
                "",
                "## 21273 Network.dll 字段名证据",
                "",
                "| group | count | examples |",
                "|---|---:|---|",
                "\n".join(name_lines),
                "",
                "## trusted full 对齐结论",
                "",
                f"- trusted full 样本数：`{trusted_summary['count']}`",
                f"- unique `(uin,item,map,pet,extend)` 组合：`{trusted_summary['unique_anchor_tuples']}`",
                f"- layout_ok 分布：`{trusted_summary['counters']['layout_ok']}`",
                f"- item_num 分布：`{trusted_summary['counters']['item_num']}`",
                f"- map_num 分布：`{trusted_summary['counters']['map_num']}`",
                f"- pet_num 分布：`{trusted_summary['counters']['pet_num']}`",
                f"- extend_item_num 分布：`{trusted_summary['counters']['extend_item_num']}`",
                "",
                "## body/full 对齐结论",
                "",
                f"- body 样本数：`{body_compare['body_total']}`",
                f"- body == full[0x0E:] 命中：`{body_compare['body_matches_full_minus_0x0e']}`",
                f"- 未命中：`{body_compare['body_unmatched']}`",
                "",
                "## cache_after 对齐结论",
                "",
                f"- cache_after full-view 样本数：`{cache_after_summary['count']}`",
                f"- unique `(uin,item,map,pet,extend)` 组合：`{cache_after_summary['unique_anchor_tuples']}`",
                f"- layout_ok 分布：`{cache_after_summary['counters']['layout_ok']}`",
                "",
                "## before_inner / TaskBlock 对齐结论",
                "",
                f"- before_inner 样本数：`{before_inner_task_summary.get('count', 0)}`",
                f"- loginpara decoder OK：`{before_inner_task_summary.get('decoded_ok', 0)}`",
                f"- cursor 完整消费：`{before_inner_task_summary.get('fully_consumed', 0)}`",
                f"- `obj+0x{TASK_COUNT_OBJECT_OFFSET:X}` / `varint_0x310328` task count 分布：`{before_inner_task_summary.get('task_num_counter')}`",
                f"- `obj+0x{TASK_ARRAY_OBJECT_OFFSET:X}` / `subobj_array_0x310330` task array count 分布：`{before_inner_task_summary.get('task_array_count_counter')}`",
                f"- 当前可解码 before_inner 是否全部 taskNum=0：`{before_inner_task_summary.get('task_num_all_decodable_zero')}`",
                "",
                "结论：`CSSafeSTPlayerDBTaskInfo` 字段名、对象位置和记录写入顺序已经由 21273 `Network.dll`/IDA export 证明；但当前 `packet_dumps_*` 中所有可解码 before_inner 的 `m_shTaskNum` 都是 0，没有任何 `m_astPlayerDBTaskInfo[i]` 实例字节可对齐到 317329 full 展开区。因此 v1.1/v1.2 不能伪造 full offset，只能把该块标为 `Unaligned / no non-zero task sample`。",
                "",
                "### CSSafeSTPlayerDBTaskInfo 记录顺序证据",
                "",
                "- ResponseLogin 对象证据：`obj+0x310328` 是 task count，`obj+0x310330` 是 task array；safe-object stride=`320`，capacity=`1100`。",
                "- 记录字段顺序证据：`sub_1800D2CB0` / `CNotifyAddTaskInfoPara` 按 24-byte record 打印 8 个 `m_astPlayerDBTaskInfo[i].*` 字段。",
                "",
                "| record offset | size | type | field |",
                "|---:|---:|---|---|",
                "\n".join(task_field_lines),
                "",
                "### IDA xref 摘要",
                "",
                "| ref rva | function | function rva | string |",
                "|---:|---|---:|---|",
                "\n".join(xref_lines),
                "",
                "## 不能直接按 full offset 对齐的类别",
                "",
                "- `cache_before_not_raw_layout`：过滤/缓存前视图，数量字段会出现不可信值，不能作为官服 raw layout。",
                "- `before_inner_encoded_or_safe_layout`：长度约 2.6KB-20KB，内容是 CSSafe wire/object 形态；可用 `qqspeed_decoder.loginpara` 做对象字段证据，但不等同 317329 full 展开 offset。",
                "- `client_request_serialize_body`：客户端登录请求，不是 `CSSafeCResponseLoginPara` 响应结构。",
                "",
                "## 官服 count=0 的填法",
                "",
                "- 数量字段为 0 时，只填 count 本身：例如 `m_shTaskNum = 0`。",
                "- 对应数组显示为空：例如 `m_astPlayerDBTaskInfo[] = empty`。",
                "- 不生成 `m_astPlayerDBTaskInfo[0].m_iTaskID = 0`，因为这会把“没有第 0 条记录”误写成“存在一条全 0 记录”。",
                "- 对 `m_astPlayerDBItemInfo[]` / `m_astRecord[]` / `m_astPetInfo[]` 这种已经由 full 长度证明存在固定展开槽位的区域，count=0 时仍可显示 `ReservedSlots`，但 active record 数为 0。",
                "- 对 `m_astPlayerDBTaskInfo[]` 这种当前只在 before_inner 对象层证明的动态数组，count=0 时只显示 empty + record layout template，不给 full offset 编假记录。",
                "",
                "### 已套用 count=0 规则的数组",
                "",
                "| array | count field | layout | count=0 display |",
                "|---|---|---|---|",
                "\n".join(
                    f"| `{item['array']}` | `{item['count']}` | {item['layout']} | {item['zero_fill']} |"
                    for item in COUNTED_ARRAY_POLICIES
                ),
                "",
                "## 当前仍保留 UnknownName / Unaligned 的 full offset 清单",
                "",
                "| range | field/block | status | reason |",
                "|---|---|---|---|",
                "\n".join(
                    f"| `{item['range']}` | `{item['name']}` | `{item['status']}` | {item['reason']} |"
                    for item in UNRESOLVED_FULL_OFFSETS
                ),
                "",
                "## 后续落 UI 的规则",
                "",
                "1. 字段名只从 `F:\\new1\\dll\\new\\21273\\Network.dll` 字符串/RTTI/反编译确认。",
                "2. offset 只从 trusted full 和 body/full 对齐确认。",
                "3. `m_astPlayerDBTaskInfo[]` 当前 dumps 的 taskNum 全为 0，按官服表达为 `VerifiedEmpty + Template`；非零记录的 317329 full offset 仍需 taskNum>0 样本。",
                "4. 未定位字段显示 `UnknownName`，但必须带 `name source / offset source / unresolved reason`。",
                "",
            ]
        ),
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Align all MsgID 100 payloads against current 21273 evidence.")
    parser.add_argument("--payload-root", default=r"F:\PacketDumpTool\PacketDumpTool\x64\Release", type=Path)
    parser.add_argument("--network-dll", default=r"F:\new1\dll\new\21273\Network.dll", type=Path)
    parser.add_argument(
        "--out",
        default=r"F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\docs\msg100_alignment_report.md",
        type=Path,
    )
    parser.add_argument(
        "--json-out",
        default=r"F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\docs\msg100_alignment_report.json",
        type=Path,
    )
    parser.add_argument(
        "--ida-export",
        default=r"F:\new1\dll\new\21273\ida_export_Network_20260613_125530",
        type=Path,
    )
    args = parser.parse_args()

    records = iter_records(args.payload_root)
    names = extract_network_names(args.network_dll)
    trusted_summary = summarize_anchor_sets(records, {"trusted_full"})
    cache_after_summary = summarize_anchor_sets(records, {"cache_after_full_view"})
    body_compare = compare_bodies(records)
    before_inner_task_summary = decode_before_inner_task_summary(records)
    ida_task_evidence = extract_ida_task_evidence(args.ida_export, args.network_dll)

    write_markdown(
        args.out,
        args.payload_root,
        args.network_dll,
        records,
        names,
        trusted_summary,
        cache_after_summary,
        body_compare,
        before_inner_task_summary,
        ida_task_evidence,
    )
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(
        json.dumps(
            {
                "records": [asdict(record) for record in records],
                "network_names": names,
                "trusted_summary": trusted_summary,
                "cache_after_summary": cache_after_summary,
                "body_compare": body_compare,
                "before_inner_task_summary": before_inner_task_summary,
                "ida_task_evidence": ida_task_evidence,
                "unresolved_full_offsets": UNRESOLVED_FULL_OFFSETS,
                "counted_array_policies": COUNTED_ARRAY_POLICIES,
            },
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )
    print(f"records={len(records)}")
    print(f"report={args.out}")
    print(f"json={args.json_out}")
    print(f"body_compare={body_compare}")
    print(f"before_inner_task={before_inner_task_summary.get('task_num_counter')}")


if __name__ == "__main__":
    main()
