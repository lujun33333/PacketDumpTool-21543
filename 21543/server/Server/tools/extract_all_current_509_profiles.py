#!/usr/bin/env python3
"""Extract every legacy Msg509 mode profile with the current client decoder."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import extract_current_509_codec as base
import extract_current_509_generic as generic


PROFILES = (
    [("border", count) for count in range(1, 7)]
    + [("anonymous", count) for count in range(1, 7)]
    + [("ranked", count) for count in range(1, 7)]
    + [("custom", count) for count in range(1, 13)]
    + [("aurora", 1)]
)


def write_u16(raw: bytearray, offset: int, value: int) -> None:
    raw[offset : offset + 2] = value.to_bytes(2, "little")


def source_raw(root: Path, count: int) -> bytearray:
    source_count = min(count, 6)
    data = json.loads(
        (root / f"safe-normal-{source_count}p.current.json").read_text(
            encoding="utf-8"
        )
    )
    return bytearray.fromhex(data["actual_raw"])[: base.OLD_FIXED_RAW_SIZE]


def prepare_raw(root: Path, profile: str, count: int) -> bytearray:
    raw = source_raw(root, count)
    raw[0x796] = count
    raw[0x9B0D] = 0
    if profile == "border":
        write_u16(raw, 0x8E65, 1)
        write_u16(raw, 0x8E67, 1)
        raw[0x74B] = 0
    elif profile == "anonymous":
        write_u16(raw, 0x8E65, 1)
        write_u16(raw, 0x8E67, 1)
        raw[0x9B0D] = count
    elif profile == "ranked":
        write_u16(raw, 0x8E65, 40)
        write_u16(raw, 0x8E67, 1)
    elif profile == "custom":
        write_u16(raw, 0x8E65, 42)
        write_u16(raw, 0x8E67, 27)
    elif profile == "aurora":
        write_u16(raw, 0x8E65, 42)
        write_u16(raw, 0x8E67, 29)
    else:
        raise ValueError(f"unknown profile {profile}")
    return raw


def extract_exact(dll: Path, raw: bytearray) -> dict:
    for _ in range(4):
        expected = bytes(raw) + bytes(base.FIXED_RAW_SIZE - len(raw))
        result = generic.recover(dll, expected)
        if result["decoder_result"] != 1:
            raise RuntimeError(
                f"client rejected trace result={result['decoder_result']}"
            )
        actual = bytes.fromhex(result["actual_raw"])
        differences = [
            index
            for index, (left, right) in enumerate(zip(expected, actual))
            if left != right
        ]
        if not differences:
            return result
        clearable = [
            index
            for index in differences
            if index < len(raw) and raw[index] != 0 and actual[index] == 0
        ]
        if len(clearable) != len(differences):
            detail = [
                (index, expected[index], actual[index])
                for index in differences[:20]
            ]
            raise RuntimeError(f"non-clearable raw mismatches: {detail}")
        for index in clearable:
            raw[index] = 0
        print(f"  normalized ignored raw offsets={clearable}", flush=True)
    raise RuntimeError("raw profile did not converge after normalization")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()

    for profile, count in PROFILES:
        output = args.root / f"safe-{profile}-{count}p.current.json"
        raw_path = args.root / f"raw-{profile}-{count}p.bin"
        if output.exists():
            existing = json.loads(output.read_text(encoding="utf-8"))
            if existing.get("decoder_result") == 1 and existing.get("raw_exact"):
                print(f"skip {profile}-{count}p exact", flush=True)
                continue
        print(f"extract {profile}-{count}p", flush=True)
        raw = prepare_raw(args.root, profile, count)
        result = extract_exact(args.dll, raw)
        raw_path.write_bytes(raw)
        output.write_text(json.dumps(result, indent=1), encoding="utf-8")
        print(
            f"accepted {profile}-{count}p wire={result['wire_size']} "
            f"tables={len(result['tables'])}",
            flush=True,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
