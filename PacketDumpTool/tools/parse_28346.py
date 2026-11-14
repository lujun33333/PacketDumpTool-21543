#!/usr/bin/env python3
"""
Parse QQSpeed Network.dll msg 28346 dumps.

This handles the dump shapes emitted by PacketDumpHook:
- raw 28346 body before the Network.dll body parser
- special28346_before_full / safe28346_builder_payload_full payload structs
- decoded inner MSGHEAD+body blobs

Use --extract to write parsed inner payloads next to the input file.
"""

from __future__ import annotations

import argparse
import json
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


DEFAULT_PROFILE = Path(r"C:\Users\27462\Desktop\bn_obf_decode_dumps\sub_180346980_profile.json")

MSGHEAD_SIZE = 14
SAFE_PAYLOAD_SIZE = 0x7DD5
SAFE_MODE_OFFSET = 0x7DD4
SAFE_INNER_OFFSET = 0x0C


def u8(data: bytes, off: int) -> int | None:
    return data[off] if 0 <= off < len(data) else None


def u16be(data: bytes, off: int) -> int | None:
    if off < 0 or off + 2 > len(data):
        return None
    return struct.unpack_from(">H", data, off)[0]


def u16le(data: bytes, off: int) -> int | None:
    if off < 0 or off + 2 > len(data):
        return None
    return struct.unpack_from("<H", data, off)[0]


def u32be(data: bytes, off: int) -> int | None:
    if off < 0 or off + 4 > len(data):
        return None
    return struct.unpack_from(">I", data, off)[0]


def u32le(data: bytes, off: int) -> int | None:
    if off < 0 or off + 4 > len(data):
        return None
    return struct.unpack_from("<I", data, off)[0]


def hex_preview(data: bytes, off: int = 0, count: int = 32) -> str:
    if off < 0:
        off = 0
    return data[off:off + count].hex(" ")


def ascii_preview(data: bytes, off: int = 0, count: int = 64) -> str:
    chunk = data[off:off + count]
    return "".join(chr(x) if 32 <= x < 127 else "." for x in chunk)


@dataclass
class MsgHead:
    msg_id: int
    msg_type: int
    msg_seq: int
    src_fe: int
    dst_fe: int
    src_id: int
    dst_id: int
    body_len_field: int

    @property
    def body_len_minus_2(self) -> int:
        return self.body_len_field - 2


def parse_msghead(data: bytes, off: int = 0) -> MsgHead | None:
    if off < 0 or off + MSGHEAD_SIZE > len(data):
        return None
    msg_id, msg_type, msg_seq = struct.unpack_from(">HhI", data, off)
    src_fe = data[off + 8]
    dst_fe = data[off + 9]
    src_id, dst_id, body_len = struct.unpack_from(">hhH", data, off + 10)
    return MsgHead(msg_id, msg_type, msg_seq, src_fe, dst_fe, src_id, dst_id, body_len)


def print_msghead(label: str, head: MsgHead | None) -> None:
    if not head:
        print(f"{label}: <not enough bytes>")
        return
    print(
        f"{label}: msg={head.msg_id} type={head.msg_type} seq={head.msg_seq} "
        f"srcFe={head.src_fe} dstFe={head.dst_fe} srcId={head.src_id} dstId={head.dst_id} "
        f"bodyLenField={head.body_len_field} bodyLenMinus2={head.body_len_minus_2}"
    )


class Mode0Codec:
    def __init__(self, profile_path: Path) -> None:
        profile = json.loads(profile_path.read_text(encoding="utf-8"))
        self.tables: dict[str, list[int]] = {}
        for item in profile:
            name = item["name"]
            self.tables[name] = [int(x, 16) if isinstance(x, str) else int(x) for x in item["table"]]

    def decode_varint(self, data: bytes, pos: int, table_name: str, max_bytes: int = 5) -> tuple[int, int, list[int], list[int]]:
        table = self.tables[table_name]
        value = 0
        shift = 0
        raw: list[int] = []
        tmp: list[int] = []
        for _ in range(max_bytes):
            if pos >= len(data):
                raise ValueError(f"truncated varint for {table_name}")
            rb = data[pos]
            pos += 1
            decoded = table[rb]
            raw.append(rb)
            tmp.append(decoded)
            value |= (decoded & 0x7F) << shift
            if decoded < 0x80:
                return value, pos, raw, tmp
            shift += 7
        raise ValueError(f"unterminated varint for {table_name}")

    def decode_mode0_stream(self, stream: bytes) -> dict[str, object]:
        pos = 0
        field_0c, pos, raw_0c, tmp_0c = self.decode_varint(stream, pos, "field_0c_varint_byte_raw_to_tmp")
        field_12, pos, raw_12, tmp_12 = self.decode_varint(stream, pos, "field_12_varint_byte_raw_to_tmp")
        payload_len, pos, raw_16, tmp_16 = self.decode_varint(stream, pos, "field_16_varint_byte_raw_to_tmp")
        payload_end = pos + payload_len
        if payload_end > len(stream):
            raise ValueError(f"mode0 payload truncated: need {payload_len} bytes at 0x{pos:X}, have {len(stream) - pos}")
        # Current server-side mode0 construction writes target_byte - 45.
        # sub_180346980 restores the original inner MSGHEAD+body bytes.
        inner_blob = bytes((x + 45) & 0xFF for x in stream[pos:payload_end])
        return {
            "field_0c": field_0c,
            "field_12": field_12,
            "payload_len": payload_len,
            "header_len": pos,
            "payload_end": payload_end,
            "raw_varints": {
                "field_0c": raw_0c,
                "field_12": raw_12,
                "payload_len": raw_16,
            },
            "tmp_varints": {
                "field_0c": tmp_0c,
                "field_12": tmp_12,
                "payload_len": tmp_16,
            },
            "inner_blob": inner_blob,
        }


def looks_like_special_payload(data: bytes) -> bool:
    if len(data) == SAFE_PAYLOAD_SIZE:
        return True
    # Be conservative. Large decoded msg100 bodies can also have 0/1 at 0x7DD4
    # and zero padding-looking ranges, so only auto-classify the fixed-size
    # Network.dll safe payload object.
    return False


def looks_like_raw_28346_body(data: bytes) -> bool:
    if len(data) < MSGHEAD_SIZE:
        return False
    inner_msg = u16be(data, 10)
    inner_len = u16be(data, 12)
    if inner_msg is None or inner_len is None:
        return False
    # Raw 28346 body produced by the server side safe wrapper:
    #   u32 seed/time? + u32 seed/time? + u16 innerMsg + u16 payloadLen + payload + mode byte/tail.
    return inner_msg in (100, 112, 223, 271, 551, 24251, 28346) and MSGHEAD_SIZE + inner_len <= len(data)


def looks_like_msghead_full(data: bytes) -> bool:
    head = parse_msghead(data, 0)
    if not head or head.body_len_field < 2:
        return False
    expected_len = MSGHEAD_SIZE + head.body_len_minus_2
    return expected_len == len(data) and head.msg_id in (1, 90, 100, 112, 223, 271, 551, 28346)


def extract_raw_28346_body(data: bytes) -> dict[str, object]:
    declared_outer_len = u16be(data, 0)
    seed0 = u32be(data, 2)
    seed1 = u32be(data, 6)
    inner_msg = u16be(data, 10)
    payload_len = u16be(data, 12)
    payload = b""
    mode = None
    tail = b""
    if payload_len is not None and MSGHEAD_SIZE + payload_len <= len(data):
        payload = data[MSGHEAD_SIZE:MSGHEAD_SIZE + payload_len]
        tail = data[MSGHEAD_SIZE + payload_len:]
        if tail:
            mode = tail[0]
    return {
        "declared_outer_len": declared_outer_len,
        "seed0": seed0,
        "seed1": seed1,
        "inner_msg": inner_msg,
        "payload_len": payload_len,
        "payload": payload,
        "mode": mode,
        "tail": tail,
    }


def extract_special_payload(data: bytes) -> dict[str, object]:
    mode = u8(data, SAFE_MODE_OFFSET)
    inner_msg = u16le(data, 0x08)
    inner_len = u16le(data, 0x0A)
    inner_payload = b""
    padding = b""
    if inner_len is not None and SAFE_INNER_OFFSET + inner_len <= len(data):
        inner_payload = data[SAFE_INNER_OFFSET:SAFE_INNER_OFFSET + inner_len]
        padding = data[SAFE_INNER_OFFSET + inner_len:SAFE_MODE_OFFSET]
    return {
        "seed0": u32le(data, 0),
        "seed1": u32le(data, 4),
        "inner_msg": inner_msg,
        "inner_len": inner_len,
        "mode": mode,
        "inner_payload": inner_payload,
        "padding_nonzero": sum(1 for x in padding if x),
        "padding_len": len(padding),
    }


def write_extract(base: Path, suffix: str, data: bytes) -> None:
    out = base.with_name(f"{base.stem}{suffix}")
    out.write_bytes(data)
    print(f"  wrote {out} ({len(data)} bytes)")


def parse_one(path: Path, codec: Mode0Codec | None, extract: bool, force_shape: str) -> None:
    data = path.read_bytes()
    print(f"\n== {path} ==")
    print(f"len={len(data)} (0x{len(data):X})")
    print(f"head32={hex_preview(data, 0, 32)}")
    print(f"ascii={ascii_preview(data, 0, 64)}")

    if force_shape == "raw" or (force_shape == "auto" and looks_like_raw_28346_body(data)):
        raw = extract_raw_28346_body(data)
        payload = raw["payload"]
        print("shape=raw_28346_body")
        print(
            f"  declaredOuterLen={raw['declared_outer_len']} seed0=0x{raw['seed0']:08X} seed1=0x{raw['seed1']:08X} "
            f"innerMsg={raw['inner_msg']} payloadLen={raw['payload_len']} modeAfterPayload={raw['mode']} "
            f"tailLen={len(raw['tail'])}"
        )
        print(f"  payloadHead={hex_preview(payload, 0, 32)}")
        if raw["tail"]:
            print(f"  tail={hex_preview(raw['tail'], 0, min(len(raw['tail']), 64))}")
        if extract and payload:
            write_extract(path, f".inner_msg{raw['inner_msg']}_payload.bin", payload)
        if raw["mode"] == 0 and codec and payload:
            decode_mode0(path, codec, payload, extract, label="  ")
        return

    if force_shape == "special" or (force_shape == "auto" and looks_like_special_payload(data)):
        sp = extract_special_payload(data)
        inner_payload = sp["inner_payload"]
        print("shape=special28346_payload_struct")
        print(
            f"  seed0=0x{sp['seed0']:08X} seed1=0x{sp['seed1']:08X} mode={sp['mode']} "
            f"innerMsg={sp['inner_msg']} innerLen={sp['inner_len']} "
            f"paddingNonzero={sp['padding_nonzero']}/{sp['padding_len']}"
        )
        print(f"  innerPayloadHead={hex_preview(inner_payload, 0, 32)}")
        if extract and inner_payload:
            write_extract(path, f".inner_msg{sp['inner_msg']}_payload.bin", inner_payload)
        if sp["mode"] == 0 and codec and inner_payload:
            decode_mode0(path, codec, inner_payload, extract, label="  ")
        return

    head = parse_msghead(data, 0)
    if force_shape == "full" or (force_shape == "auto" and looks_like_msghead_full(data)):
        print("shape=possible_msghead_full_or_inner_blob")
        print_msghead("  msghead", head)
        body = data[MSGHEAD_SIZE:]
        print(f"  bodyHead={hex_preview(body, 0, 32)}")
        if extract:
            write_extract(path, f".msg{head.msg_id}_body.bin", body)
        return

    print("shape=unknown")


def decode_mode0(path: Path, codec: Mode0Codec, payload: bytes, extract: bool, label: str = "") -> None:
    try:
        decoded = codec.decode_mode0_stream(payload)
    except Exception as exc:
        print(f"{label}mode0Decode=failed: {exc}")
        return
    inner_blob = decoded["inner_blob"]
    head = parse_msghead(inner_blob, 0)
    print(
        f"{label}mode0Decode=ok field0c={decoded['field_0c']} field12={decoded['field_12']} "
        f"innerBlobLen={decoded['payload_len']} varintHeaderLen={decoded['header_len']}"
    )
    print_msghead(f"{label}mode0InnerHead", head)
    print(f"{label}mode0InnerHeadBytes={hex_preview(inner_blob, 0, 32)}")
    if extract:
        write_extract(path, ".mode0_inner_full.bin", inner_blob)
        if len(inner_blob) >= MSGHEAD_SIZE:
            write_extract(path, ".mode0_inner_body.bin", inner_blob[MSGHEAD_SIZE:])


def summarize_one(path: Path) -> None:
    data = path.read_bytes()
    if looks_like_raw_28346_body(data):
        raw = extract_raw_28346_body(data)
        print(
            f"{path.name}\traw28346\tlen={len(data)}\tinner={raw['inner_msg']}\t"
            f"payloadLen={raw['payload_len']}\tmode={raw['mode']}\ttailLen={len(raw['tail'])}\t"
            f"seed0=0x{raw['seed0']:08X}\tseed1=0x{raw['seed1']:08X}"
        )
        return
    if looks_like_special_payload(data):
        sp = extract_special_payload(data)
        print(
            f"{path.name}\tspecialPayload\tlen={len(data)}\tinner={sp['inner_msg']}\t"
            f"innerLen={sp['inner_len']}\tmode={sp['mode']}\tpaddingNonzero={sp['padding_nonzero']}"
        )
        return
    head = parse_msghead(data, 0)
    if looks_like_msghead_full(data):
        print(
            f"{path.name}\tmsgheadFull\tlen={len(data)}\tmsg={head.msg_id}\t"
            f"type={head.msg_type}\tbodyLenMinus2={head.body_len_minus_2}"
        )
        return
    print(f"{path.name}\tunknown\tlen={len(data)}")


def main(argv: Iterable[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Parse QQSpeed msg 28346 dump files")
    ap.add_argument("files", nargs="+", type=Path)
    ap.add_argument("--profile", type=Path, default=DEFAULT_PROFILE, help="sub_180346980 profile json for mode0 decoding")
    ap.add_argument("--extract", action="store_true", help="write extracted inner payload/body files")
    ap.add_argument("--summary", action="store_true", help="print one-line summary for each file")
    ap.add_argument(
        "--as",
        dest="force_shape",
        choices=("auto", "raw", "special", "full"),
        default="auto",
        help="force input shape: raw 28346 body, special payload struct, or MSGHEAD+body blob",
    )
    args = ap.parse_args(argv)

    if args.summary:
        for file in args.files:
            summarize_one(file)
        return 0

    codec = None
    if args.profile.exists():
        codec = Mode0Codec(args.profile)
    else:
        print(f"warning: profile not found, mode0 decode disabled: {args.profile}")

    for file in args.files:
        parse_one(file, codec, args.extract, args.force_shape)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
