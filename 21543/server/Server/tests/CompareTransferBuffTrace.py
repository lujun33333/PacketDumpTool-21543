#!/usr/bin/env python3
"""Compare server-side Msg153 Buff fingerprints with client Msg560 body dumps."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class BuffKey:
    src_uin: int
    ver: int
    seq: int
    buff_len: int
    sha256: str


@dataclass(frozen=True)
class Dump560:
    path: Path
    self_len: int | None
    src_uin: int
    src_player_id: int
    ver: int
    seq: int
    buff: bytes

    @property
    def key(self) -> BuffKey:
        return BuffKey(
            src_uin=self.src_uin,
            ver=self.ver,
            seq=self.seq,
            buff_len=len(self.buff),
            sha256=hashlib.sha256(self.buff).hexdigest().upper(),
        )


def read_u16be(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "big")


def read_u32be(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "big")


def parse_msg560_body(path: Path, raw: bytes) -> Dump560:
    if len(raw) < 13:
        raise ValueError(f"too short: {len(raw)} bytes")

    base = 0
    self_len: int | None = None
    if len(raw) >= 15:
        declared = read_u16be(raw, 0)
        if declared == len(raw):
            self_len = declared
            base = 2

    if len(raw) - base < 13:
        raise ValueError(f"body too short after SelfLen: {len(raw) - base} bytes")

    src_uin = read_u32be(raw, base)
    src_player_id = read_u16be(raw, base + 4)
    ver = raw[base + 6]
    seq = read_u32be(raw, base + 7)
    buff_len = read_u16be(raw, base + 11)
    expected = base + 13 + buff_len
    if expected != len(raw):
        raise ValueError(
            f"length mismatch: base={base}, BuffLen={buff_len}, "
            f"expected={expected}, actual={len(raw)}"
        )

    return Dump560(
        path=path,
        self_len=self_len,
        src_uin=src_uin,
        src_player_id=src_player_id,
        ver=ver,
        seq=seq,
        buff=raw[base + 13 : expected],
    )


def iter_dump_paths(root: Path) -> Iterable[Path]:
    if root.is_file():
        yield root
        return
    yield from sorted(root.rglob("*inner_dispatch_body_msg560*.bin"))


def load_dumps(root: Path) -> tuple[list[Dump560], list[str]]:
    parsed: list[Dump560] = []
    errors: list[str] = []
    for path in iter_dump_paths(root):
        try:
            parsed.append(parse_msg560_body(path, path.read_bytes()))
        except (OSError, ValueError) as exc:
            errors.append(f"{path}: {exc}")
    return parsed, errors


def load_server_forwards(
    path: Path, dst_uin: int | None
) -> tuple[list[tuple[BuffKey, dict]], list[str], Counter[str]]:
    forwards: list[tuple[BuffKey, dict]] = []
    errors: list[str] = []
    outcomes: Counter[str] = Counter()
    with path.open("r", encoding="utf-8-sig", errors="replace") as stream:
        for line_no, raw_line in enumerate(stream, 1):
            line = raw_line.strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError as exc:
                errors.append(f"{path}:{line_no}: invalid JSON: {exc}")
                continue
            if event.get("event") != "msg560_forward":
                continue
            if dst_uin is not None and int(event.get("dst_uin", -1)) != dst_uin:
                continue

            outcome = str(event.get("outcome", "unknown"))
            outcomes[outcome] += 1
            if outcome != "sent" or event.get("send_ok") is not True:
                continue
            try:
                digest = str(event["buff_sha256"]).upper()
                if len(digest) != 64 or any(ch not in "0123456789ABCDEF" for ch in digest):
                    raise ValueError("invalid SHA-256")
                key = BuffKey(
                    src_uin=int(event["src_uin"]),
                    ver=int(event["ver"]),
                    seq=int(event["seq"]),
                    buff_len=int(event["buff_len"]),
                    sha256=digest,
                )
            except (KeyError, TypeError, ValueError) as exc:
                errors.append(f"{path}:{line_no}: malformed forward event: {exc}")
                continue
            forwards.append((key, event))
    return forwards, errors, outcomes


def format_key(key: BuffKey) -> str:
    return (
        f"src={key.src_uin} ver={key.ver} seq={key.seq} "
        f"len={key.buff_len} sha256={key.sha256}"
    )


def compare(
    trace_path: Path,
    dump_root: Path,
    dst_uin: int | None,
    show: int,
) -> int:
    forwards, trace_errors, outcomes = load_server_forwards(trace_path, dst_uin)
    dumps, dump_errors = load_dumps(dump_root)

    server_counts = Counter(key for key, _ in forwards)
    dump_counts = Counter(item.key for item in dumps)
    matched_counts = server_counts & dump_counts
    server_only = server_counts - matched_counts
    dump_only = dump_counts - matched_counts

    matched = sum(matched_counts.values())
    sent = sum(server_counts.values())
    received = sum(dump_counts.values())

    print("153 -> 560 Buff comparison")
    print(f"  trace:       {trace_path}")
    print(f"  dumps:       {dump_root}")
    print(f"  dst_uin:     {dst_uin if dst_uin is not None else 'ALL (prefer --dst-uin)'}")
    print(f"  outcomes:    {dict(outcomes)}")
    print(f"  sent events: {sent}")
    print(f"  560 dumps:   {received}")
    print(f"  exact match: {matched}")
    print(f"  server only: {sum(server_only.values())}")
    print(f"  dump only:   {sum(dump_only.values())}")
    print(f"  parse errors:{len(trace_errors) + len(dump_errors)}")

    if server_only:
        print("\nServer forwards without an identical client 560 dump:")
        for key, count in list(server_only.items())[:show]:
            print(f"  x{count} {format_key(key)}")
    if dump_only:
        print("\nClient 560 dumps without an identical server forward:")
        for key, count in list(dump_only.items())[:show]:
            print(f"  x{count} {format_key(key)}")
    if trace_errors or dump_errors:
        print("\nParse errors:")
        for error in (trace_errors + dump_errors)[:show]:
            print(f"  {error}")

    if trace_errors or dump_errors:
        return 2
    if sent == 0 or received == 0:
        return 3
    if server_only or dump_only:
        return 4
    return 0


def self_test() -> int:
    # Official 21487 Msg560 body captured by PacketDumpTool. The first two bytes
    # are the serialized SelfLen (71), followed by a 69-byte logical body.
    raw = bytes.fromhex(
        "0047"
        "9DDD9D84"
        "01A4"
        "00"
        "00000000"
        "0038"
        "67907FC366321A6C935C171B9A51297E9E763944E067B9116BEB930294668CD1"
        "2BD43F2B134DD36EEADFB890E585CCA2655323781E0E1DDC"
    )
    parsed = parse_msg560_body(Path("<official-fixture>"), raw)
    expected_sha = "E6DA4042E129B69515EFFA0B41FAD010A8F714B4F2DBB66EB2B57ACFA6C88438"
    checks = (
        parsed.self_len == 71,
        parsed.src_uin == 2648546692,
        parsed.src_player_id == 420,
        parsed.ver == 0,
        parsed.seq == 0,
        len(parsed.buff) == 56,
        parsed.key.sha256 == expected_sha,
    )
    if not all(checks):
        print(f"self-test failed: {parsed}", file=sys.stderr)
        return 1
    print(
        "self-test passed: SelfLen=71 SrcUin=2648546692 "
        f"PlayerID=420 BuffLen=56 SHA256={expected_sha}"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare server Msg153 Buff hashes with PacketDumpTool Msg560 body dumps."
    )
    parser.add_argument("--server-log", type=Path, help="RelayBuffTrace JSONL file")
    parser.add_argument(
        "--client-dumps",
        type=Path,
        help="recipient PacketDumpTool dump directory, or one Msg560 .bin file",
    )
    parser.add_argument(
        "--dst-uin",
        type=int,
        help="only compare server forwards sent to this recipient UIN",
    )
    parser.add_argument("--show", type=int, default=8, help="maximum mismatch/error rows")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if args.server_log is None or args.client_dumps is None:
        parser.error("--server-log and --client-dumps are required unless --self-test is used")
    if not args.server_log.is_file():
        parser.error(f"server log does not exist: {args.server_log}")
    if not args.client_dumps.exists():
        parser.error(f"client dump path does not exist: {args.client_dumps}")
    return compare(args.server_log, args.client_dumps, args.dst_uin, max(args.show, 1))


if __name__ == "__main__":
    raise SystemExit(main())
