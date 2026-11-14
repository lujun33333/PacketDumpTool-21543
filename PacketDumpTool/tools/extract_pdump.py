#!/usr/bin/env python3
"""Extract framed PacketDumpTool captures into individual binary files."""

from __future__ import annotations

import argparse
import pathlib
import struct


HEADER = struct.Struct("<IHHIQIHHII")
MAGIC = 0x31504450
DIRECTIONS = {1: "recv", 2: "send"}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=pathlib.Path)
    parser.add_argument("-o", "--output", type=pathlib.Path)
    args = parser.parse_args()

    output = args.output or args.capture.with_suffix("")
    output.mkdir(parents=True, exist_ok=True)

    count = 0
    with args.capture.open("rb") as source:
        while True:
            raw_header = source.read(HEADER.size)
            if not raw_header:
                break
            if len(raw_header) != HEADER.size:
                raise ValueError("truncated record header")

            (
                magic,
                version,
                direction,
                sequence,
                tick,
                thread_id,
                msg_id,
                _reserved,
                original_length,
                captured_length,
            ) = HEADER.unpack(raw_header)
            if magic != MAGIC or version != 1:
                raise ValueError(f"invalid record at index {count}")

            payload = source.read(captured_length)
            if len(payload) != captured_length:
                raise ValueError(f"truncated payload at index {count}")

            direction_name = DIRECTIONS.get(direction, f"dir{direction}")
            name = (
                f"{sequence:010d}_{direction_name}_msg{msg_id}_"
                f"len{original_length}_tick{tick}_tid{thread_id}.bin"
            )
            (output / name).write_bytes(payload)
            count += 1

    print(f"extracted={count} output={output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
