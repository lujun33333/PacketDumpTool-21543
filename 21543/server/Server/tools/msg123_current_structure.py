#!/usr/bin/env python3
"""Discover current Msg123 top-level and nested count-field structure."""

from __future__ import annotations

import argparse
import ctypes
import json
from pathlib import Path

from msg123enc import Vector
from msg123patch import PinnedEncoder


CTOR = 0x34D4E0
COMPRESS = 0x387B30
WRITE = 0x3DE810
DTOR = 0x36C220
RAND_IAT = 0x4B5058
OBJ_SIZE = 0x200000
PLAIN_SIZE = 42213


class Oracle:
    def __init__(self, dll: Path):
        self.encoder = PinnedEncoder(
            dll,
            CTOR,
            COMPRESS,
            WRITE,
            DTOR,
            OBJ_SIZE,
            PLAIN_SIZE,
            RAND_IAT,
        )
        self.source = ctypes.create_string_buffer(PLAIN_SIZE)

    def encode(self, values=None, padding=1) -> bytes:
        ctypes.memset(self.source, 0, PLAIN_SIZE)
        for offset, value in (values or {}).items():
            self.source[offset] = value & 0xFF
        self.encoder.rand_value = padding - 1
        ctypes.memset(self.encoder._obj, 0, self.encoder._obj_size)
        self.encoder._ctor(ctypes.addressof(self.encoder._obj))
        try:
            self.encoder._compress(
                ctypes.addressof(self.encoder._obj),
                ctypes.addressof(self.source),
            )
            if self.encoder._vector.first:
                self.encoder._vector.last = self.encoder._vector.first
            self.encoder._write(
                ctypes.addressof(self.encoder._obj),
                ctypes.byref(self.encoder._vector),
            )
            if not self.encoder._vector.first or not self.encoder._vector.last:
                return b""
            return ctypes.string_at(
                self.encoder._vector.first,
                self.encoder._vector.last - self.encoder._vector.first,
            )
        finally:
            self.encoder._dtor(ctypes.addressof(self.encoder._obj), 0)


def first_diff(left: bytes, right: bytes) -> int:
    for index, values in enumerate(zip(left, right)):
        if values[0] != values[1]:
            return index
    return min(len(left), len(right))


def group_signatures(signatures):
    offsets = sorted(signatures)
    fields = []
    index = 0
    while index < len(offsets):
        base = offsets[index]
        wire = signatures[base]["p"][0]
        end = index + 1
        while (
            end < len(offsets)
            and offsets[end] == offsets[end - 1] + 1
            and signatures[offsets[end]]["p"][0] == wire
        ):
            end += 1
        fields.append(
            {
                "base": base,
                "width": end - index,
                "wire": wire,
                "growth": signatures[base]["d"],
            }
        )
        index = end
    return sorted(fields, key=lambda field: field["wire"])


def scan_offsets(oracle: Oracle, hold, baseline, begin, end, excluded):
    signatures = {}
    for offset in range(begin, end):
        if offset in excluded:
            continue
        values = dict(hold)
        values[offset] = 1
        wire = oracle.encode(values)
        if wire == baseline:
            continue
        signatures[offset] = {
            "d": len(wire) - len(baseline),
            "p": [first_diff(baseline, wire)],
        }
    return group_signatures(signatures)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--scan", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    scan = json.loads(args.scan.read_text(encoding="utf-8"))
    signatures = {int(key): value for key, value in scan["sigs"].items()}
    top = group_signatures(signatures)
    known = {
        offset
        for field in top
        for offset in range(field["base"], field["base"] + field["width"])
    }
    counts = [field for field in top if field["growth"] > 0]
    oracle = Oracle(args.dll)
    elements = {}
    queue = [(field, {}) for field in counts]
    processed = set()
    while queue:
        field, parent_hold = queue.pop(0)
        base = field["base"]
        if base in processed:
            continue
        processed.add(base)
        baseline = oracle.encode(parent_hold)
        hold = dict(parent_hold)
        hold[base] = 1
        one = oracle.encode(hold)
        subfields = scan_offsets(
            oracle,
            hold,
            one,
            max(0, base - 4),
            min(PLAIN_SIZE, base + 400),
            known,
        )
        elements[str(base)] = subfields
        for subfield in subfields:
            known.update(
                range(
                    subfield["base"],
                    subfield["base"] + subfield["width"],
                )
            )
        for subfield in subfields:
            if subfield["growth"] > 0:
                queue.append((subfield, hold))
        print(
            f"count={base} growth={len(one) - len(baseline)} "
            f"subfields={len(subfields)}",
            flush=True,
        )
    args.out.write_text(
        json.dumps({"top": top, "elements": elements}, indent=2),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
