#!/usr/bin/env python3
"""Map current Msg123 plain-object offsets to deterministic wire positions."""

from __future__ import annotations

import argparse
import ctypes
import json
import time
from pathlib import Path

from msg123enc import Vector
from msg123patch import PinnedEncoder


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--network-dll", type=Path, required=True)
    parser.add_argument("--ctor", required=True)
    parser.add_argument("--compress", required=True)
    parser.add_argument("--write", required=True)
    parser.add_argument("--dtor", required=True)
    parser.add_argument("--obj-size", required=True)
    parser.add_argument("--plain-size", required=True)
    parser.add_argument("--rand-iat", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    encoder = PinnedEncoder(
        args.network_dll,
        int(args.ctor, 16),
        int(args.compress, 16),
        int(args.write, 16),
        int(args.dtor, 16),
        int(args.obj_size, 0),
        int(args.plain_size, 0),
        rand_iat=int(args.rand_iat, 16),
    )
    source = ctypes.create_string_buffer(encoder.plain_size)

    def encode_current() -> bytes:
        ctypes.memset(encoder._obj, 0, encoder._obj_size)
        encoder._ctor(ctypes.addressof(encoder._obj))
        try:
            encoder._compress(
                ctypes.addressof(encoder._obj), ctypes.addressof(source)
            )
            if encoder._vector.first:
                encoder._vector.last = encoder._vector.first
            encoder._write(
                ctypes.addressof(encoder._obj),
                ctypes.byref(encoder._vector),
            )
            if not encoder._vector.first or not encoder._vector.last:
                return b""
            return ctypes.string_at(
                encoder._vector.first,
                encoder._vector.last - encoder._vector.first,
            )
        finally:
            encoder._dtor(ctypes.addressof(encoder._obj), 0)

    ctypes.memset(source, 0, encoder.plain_size)
    baseline = encode_current()
    signatures = {}
    started = time.time()
    for offset in range(encoder.plain_size):
        source[offset] = 1
        wire = encode_current()
        source[offset] = 0
        if wire == baseline:
            continue
        if len(wire) == len(baseline):
            positions = [
                index
                for index, (left, right) in enumerate(zip(baseline, wire))
                if left != right
            ]
            signatures[offset] = {"d": 0, "p": positions}
        else:
            prefix = 0
            while (
                prefix < min(len(baseline), len(wire))
                and baseline[prefix] == wire[prefix]
            ):
                prefix += 1
            signatures[offset] = {
                "d": len(wire) - len(baseline),
                "p": [prefix],
            }
        if offset and offset % 8000 == 0:
            print(
                f"{offset}/{encoder.plain_size} hits={len(signatures)} "
                f"elapsed={time.time() - started:.0f}s",
                flush=True,
            )

    args.out.write_text(
        json.dumps(
            {
                "plain_size": encoder.plain_size,
                "baseline_len": len(baseline),
                "baseline_hex": baseline.hex(),
                "sigs": {str(key): value for key, value in signatures.items()},
            }
        ),
        encoding="utf-8",
    )
    print(
        f"hits={len(signatures)} baseline={len(baseline)} "
        f"elapsed={time.time() - started:.0f}s"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
