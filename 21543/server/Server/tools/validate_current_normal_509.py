#!/usr/bin/env python3
"""Validate generated normal-room Msg509 envelopes in the current client decoder."""

from __future__ import annotations

import argparse
import pathlib

import extract_current_509_codec as codec


def validate_one(dll: pathlib.Path, envelope_path: pathlib.Path) -> None:
    envelope = envelope_path.read_bytes()
    if len(envelope) < 13:
        raise ValueError("outer envelope is truncated")
    inner_length = int.from_bytes(envelope[10:12], "big")
    inner = envelope[12 : 12 + inner_length]
    if len(inner) != inner_length or envelope[12 + inner_length :] != b"\x01":
        raise ValueError("outer envelope framing mismatch")

    uc = codec.new_unicorn(dll)
    uc.mem_write(codec.OBJ, b"\0" * codec.OBJ_SIZE)
    codec.call(uc, codec.BASE + codec.CTOR_RVA, codec.OBJ)
    uc.mem_write(codec.INP, inner)
    uc.mem_write(codec.CUR, codec.INP.to_bytes(8, "little"))
    uc.mem_write(codec.CUR + 8, (0).to_bytes(4, "little"))
    result = codec.call(
        uc,
        codec.BASE + codec.DECODER_RVA,
        codec.OBJ,
        codec.CUR,
        codec.INP + len(inner),
        codec.CUR + 8,
    )
    consumed = int.from_bytes(uc.mem_read(codec.CUR + 8, 4), "little")
    if result != 1 or consumed != len(inner):
        raise ValueError(
            f"client decoder rejected result={result} consumed={consumed}/{len(inner)}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dll", type=pathlib.Path, required=True)
    parser.add_argument("--root", type=pathlib.Path, required=True)
    parser.add_argument(
        "--all",
        action="store_true",
        help="validate every safe-*.28346.bin fixture in the root",
    )
    args = parser.parse_args()

    if args.all:
        names = sorted(path.name for path in args.root.glob("safe-*.28346.bin"))
        if not names:
            raise ValueError("no safe Msg509 fixtures found")
    else:
        names = [f"safe-normal-{count}p.28346.bin" for count in range(1, 7)]
        names.extend(
            [
                "safe-normal-mapcheck-1p.28346.bin",
                "safe-normal-speed-1p.28346.bin",
                "safe-normal-mapcheck-speed-1p.28346.bin",
            ]
        )
    for name in names:
        path = args.root / name
        try:
            validate_one(args.dll, path)
        except Exception as error:
            raise ValueError(f"{name}: {error}") from error
        print(f"accepted {name}")
    print(f"accepted_total={len(names)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
