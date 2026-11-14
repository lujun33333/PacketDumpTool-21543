from __future__ import annotations

import argparse
import hashlib
import re
from pathlib import Path


HEADER_MARKER = b"dir=parser_queue_dispatch_body msg=25066 "
TAIL_MARKER = bytes.fromhex("0331bc24003c000a000c000d")
HEX_LINE = re.compile(rb"^[0-9A-Fa-f]{8}  ")
HEX_TOKEN = re.compile(rb"(?<![0-9A-Fa-f])[0-9A-Fa-f]{2}(?![0-9A-Fa-f])")


def read_first_official_body(path: Path) -> bytes:
    with path.open("rb") as stream:
        for line in stream:
            if HEADER_MARKER not in line:
                continue
            body = bytearray()
            for dump_line in stream:
                if not HEX_LINE.match(dump_line):
                    break
                for token in HEX_TOKEN.findall(dump_line[10:58]):
                    body.append(int(token, 16))
            if not body:
                raise RuntimeError("msg25066 dump header has no hex body")
            return bytes(body)
    raise RuntimeError("cannot find parser_queue_dispatch_body msg=25066")


def widen_item_counts(tail: bytes) -> bytes:
    """Lift a pre-21669 tail so each ClientItemOpenConfig carries a uint16 item count.

    Captures taken from a client older than 21669 encode that count in a single byte.
    The serializer now emits the widened form that 21669 requires, so legacy captures
    have to be lifted before the byte comparison. Captures that already use the new
    layout are returned untouched.
    """
    cursor = 4 + 2 + 2 + 2  # switchFlag1 + three uint16 fields
    count = int.from_bytes(tail[cursor : cursor + 2], "big")
    cursor += 2
    if count == 0:
        return tail

    first_length = int.from_bytes(tail[cursor : cursor + 2], "big")
    if first_length != 2 + 2 + 1 + 4 * tail[cursor + 4]:
        return tail

    out = bytearray(tail[:cursor])
    for _ in range(count):
        record_length = int.from_bytes(tail[cursor : cursor + 2], "big")
        items = tail[cursor + 4]
        out += (2 + 2 + 2 + 4 * items).to_bytes(2, "big")
        out += tail[cursor + 2 : cursor + 4]
        out += items.to_bytes(2, "big")
        out += tail[cursor + 5 : cursor + 5 + 4 * items]
        cursor += record_length
    return bytes(out) + tail[cursor:]


def first_difference(expected: bytes, actual: bytes) -> str:
    common = min(len(expected), len(actual))
    for offset in range(common):
        if expected[offset] != actual[offset]:
            return (
                f"offset=0x{offset:X} official=0x{expected[offset]:02X} "
                f"serialized=0x{actual[offset]:02X}"
            )
    return f"length official={len(expected)} serialized={len(actual)}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("network_dump", type=Path)
    parser.add_argument("serialized_tail", type=Path)
    args = parser.parse_args()

    body = read_first_official_body(args.network_dump)
    if len(body) < 2:
        raise RuntimeError("official msg25066 body is truncated")
    payload_end = int.from_bytes(body[:2], "big")
    if payload_end > len(body):
        raise RuntimeError(
            f"official msg25066 payload length {payload_end} exceeds dump length {len(body)}"
        )

    tail_start = body.find(TAIL_MARKER)
    if tail_start < 0:
        raise RuntimeError("cannot locate Config4 tail marker in official msg25066")
    official_tail = widen_item_counts(body[tail_start:payload_end])
    serialized_tail = args.serialized_tail.read_bytes()

    if serialized_tail != official_tail:
        raise RuntimeError(
            "Config4 tail differs from official dump: "
            + first_difference(official_tail, serialized_tail)
        )

    digest = hashlib.sha256(serialized_tail).hexdigest()
    print(
        "Official msg25066 comparison passed: "
        f"offset=0x{tail_start:X}, length={len(serialized_tail)}, sha256={digest}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
