#!/usr/bin/env python3
"""Validate QQSpeed Msg153/560 framing and inner Msg123 coverage in a PDUMP."""

from __future__ import annotations

import argparse
import collections
import pathlib
import struct


PDUMP_HEADER = struct.Struct("<IHHIQIHHII")
PDUMP_MAGIC = 0x31504450


def be16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def be32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def parse_msg153(data: bytes) -> tuple[int, int, int]:
    if len(data) < 18 or be16(data, 0) != len(data):
        raise ValueError("Msg153 root length mismatch")
    cursor = 2
    _uin = be32(data, cursor)
    cursor += 4
    _timestamp = be32(data, cursor)
    cursor += 4
    _version = data[cursor]
    cursor += 1
    sequence = be32(data, cursor)
    cursor += 4
    destination_count = data[cursor]
    cursor += 1
    for _ in range(destination_count):
        if cursor + 2 > len(data):
            raise ValueError("Msg153 destination length truncated")
        record_length = be16(data, cursor)
        if record_length < 8 or cursor + record_length > len(data):
            raise ValueError("Msg153 destination record invalid")
        cursor += record_length
    if cursor + 2 > len(data):
        raise ValueError("Msg153 buffer length truncated")
    buffer_length = be16(data, cursor)
    cursor += 2
    if cursor + buffer_length > len(data):
        raise ValueError("Msg153 movement buffer truncated")
    extension_length = len(data) - (cursor + buffer_length)
    return sequence, destination_count, extension_length


def parse_msg560(data: bytes) -> tuple[int, int, int]:
    if len(data) < 15 or be16(data, 0) != len(data):
        raise ValueError("Msg560 root length mismatch")
    source_player_id = be16(data, 6)
    version = data[8]
    sequence = be32(data, 9)
    buffer_length = be16(data, 13)
    if 15 + buffer_length != len(data):
        raise ValueError("Msg560 movement buffer length mismatch")
    return source_player_id, version, sequence


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=pathlib.Path)
    args = parser.parse_args()

    record_count = 0
    trailing_partial = False
    errors: list[str] = []
    msg153_count = 0
    msg560_count = 0
    inner123_count = 0
    destination_counts: collections.Counter[int] = collections.Counter()
    msg153_extensions: collections.Counter[int] = collections.Counter()
    msg560_source_ids: collections.Counter[int] = collections.Counter()
    msg560_versions: collections.Counter[int] = collections.Counter()

    with args.capture.open("rb") as source:
        while True:
            raw_header = source.read(PDUMP_HEADER.size)
            if not raw_header:
                break
            if len(raw_header) != PDUMP_HEADER.size:
                trailing_partial = True
                break
            (
                magic,
                version,
                direction,
                sequence,
                _tick,
                _thread_id,
                msg_id,
                _reserved,
                _original_length,
                captured_length,
            ) = PDUMP_HEADER.unpack(raw_header)
            payload = source.read(captured_length)
            if len(payload) != captured_length:
                trailing_partial = True
                break
            if magic != PDUMP_MAGIC or version != 1:
                errors.append(f"record {record_count}: invalid PDUMP header")
                break

            try:
                if direction == 2 and msg_id == 153:
                    _relay_sequence, destination_count, extension_length = parse_msg153(payload)
                    msg153_count += 1
                    destination_counts[destination_count] += 1
                    msg153_extensions[extension_length] += 1
                elif direction == 1 and msg_id == 560:
                    source_player_id, relay_version, _relay_sequence = parse_msg560(payload)
                    msg560_count += 1
                    msg560_source_ids[source_player_id] += 1
                    msg560_versions[relay_version] += 1
                elif direction == 2 and msg_id == 28346:
                    if len(payload) >= 14 and be16(payload, 0) == len(payload):
                        inner_id = be16(payload, 10)
                        inner_length = be16(payload, 12)
                        if 14 + inner_length <= len(payload) and inner_id == 123:
                            inner123_count += 1
            except ValueError as exc:
                errors.append(f"record {sequence}: {exc}")
            record_count += 1

    print(
        f"records={record_count} msg153={msg153_count} msg560={msg560_count} "
        f"inner123={inner123_count} trailing_partial={int(trailing_partial)} errors={len(errors)}"
    )
    print(f"msg153_destination_counts={dict(sorted(destination_counts.items()))}")
    print(f"msg153_extension_lengths={dict(sorted(msg153_extensions.items()))}")
    print(f"msg560_versions={dict(sorted(msg560_versions.items()))}")
    print(f"msg560_source_id_count={len(msg560_source_ids)}")
    for error in errors[:20]:
        print(f"error={error}")
    six_player_baseline = (
        msg153_count > 0
        and set(destination_counts) == {5}
        and len(msg560_source_ids) == 5
    )
    print(f"six_player_baseline={int(six_player_baseline)}")
    return 1 if (
        errors
        or not six_player_baseline
        or msg560_count == 0
        or inner123_count == 0
    ) else 0


if __name__ == "__main__":
    raise SystemExit(main())
