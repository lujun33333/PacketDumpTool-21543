from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UcError
from unicorn.x86_const import *

import extract_current_509_codec as base

EXPORT_STACK = 0x7600000000
EXPORT_STACK_SIZE = 0x200000
TARGET_OVERRIDE_OFFSETS = {
    0x3AC728: (0x7A4, 4),
    0x394E52: (0xA8F9, 4),
    0x3AF2D6: (0xA85D, 1),
    0x3A976F: (0x758, 4),
    0x39952D: (0x758, 4),
}


def changed_ranges(before: bytes, after: bytes) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    start = None
    for index, (left, right) in enumerate(zip(before, after)):
        if left != right and start is None:
            start = index
        elif left == right and start is not None:
            ranges.append((start, index))
            start = None
    if start is not None:
        ranges.append((start, len(before)))
    return ranges


def recover(dll: Path, expected_raw: bytes) -> dict:
    uc = base.new_unicorn(dll)
    uc.mem_map(EXPORT_STACK, EXPORT_STACK_SIZE)
    uc.mem_write(base.OBJ, b"\0" * base.OBJ_SIZE)
    base.call(uc, base.BASE + base.CTOR_RVA, base.OBJ)
    uc.mem_write(base.INP, b"\0" * base.INP_SIZE)
    uc.mem_write(base.CUR, base.INP.to_bytes(8, "little"))
    uc.mem_write(base.CUR + 8, (0).to_bytes(4, "little"))

    parser_context = None
    state = {
        "mode": "walk",
        "pause": None,
        "reads": 0,
        "first_write": None,
        "next_read": None,
        "next_first_write": None,
        "undo": {},
    }

    def export_raw() -> bytes:
        saved = uc.context_save()
        uc.mem_write(base.OUT, b"\0" * base.FIXED_RAW_SIZE)
        rsp = (EXPORT_STACK + EXPORT_STACK_SIZE - 0x1000) & ~0xF
        rsp -= 8
        uc.mem_write(rsp, base.SENT.to_bytes(8, "little"))
        uc.reg_write(UC_X86_REG_RSP, rsp)
        uc.reg_write(UC_X86_REG_RCX, base.OBJ)
        uc.reg_write(UC_X86_REG_RDX, base.OUT)
        uc.reg_write(UC_X86_REG_R8, 0)
        uc.reg_write(UC_X86_REG_R9, 0)
        uc.reg_write(UC_X86_REG_RIP, base.BASE + base.EXPORTER_RVA)
        uc.emu_start(base.BASE + base.EXPORTER_RVA, base.SENT, count=100_000_000)
        raw = bytes(uc.mem_read(base.OUT, base.FIXED_RAW_SIZE))
        uc.context_restore(saved)
        return raw

    def read_hook(emu, access, address, size, value, user):
        if not (base.INP <= address < base.INP + base.INP_SIZE):
            return
        rva = emu.reg_read(UC_X86_REG_RIP) - base.BASE
        offset = address - base.INP
        if state["mode"] == "walk":
            state["pause"] = {
                "rva": rva,
                "offset": offset,
                "context": emu.context_save(),
                "rip": emu.reg_read(UC_X86_REG_RIP),
            }
            emu.emu_stop()
        else:
            state["reads"] += 1
            if state["reads"] >= 2:
                state["next_read"] = {"rva": rva, "offset": offset}
                if state["mode"] != "probe":
                    emu.emu_stop()

    def write_hook(emu, access, address, size, value, user):
        if state["mode"] not in ("probe", "map"):
            return
        for index in range(size):
            target = address + index
            if target not in state["undo"]:
                try:
                    state["undo"][target] = emu.mem_read(target, 1)[0]
                except UcError:
                    pass
        if (
            state["mode"] in ("probe", "map")
            and base.OBJ <= address < base.OBJ + base.OBJ_SIZE
            and state["reads"] == 1
            and state["first_write"] is None
        ):
            state["first_write"] = {
                "obj": address - base.OBJ,
                "size": size,
                "value": value,
                "rva": emu.reg_read(UC_X86_REG_RIP) - base.BASE,
            }
        elif (
            state["mode"] == "probe"
            and base.OBJ <= address < base.OBJ + base.OBJ_SIZE
            and state["reads"] >= 2
            and state["next_first_write"] is None
        ):
            state["next_first_write"] = {
                "obj": address - base.OBJ,
                "size": size,
                "value": value,
                "rva": emu.reg_read(UC_X86_REG_RIP) - base.BASE,
            }
            emu.emu_stop()

    read_handle = uc.hook_add(UC_HOOK_MEM_READ, read_hook)
    write_handle = uc.hook_add(UC_HOOK_MEM_WRITE, write_hook)

    def restore_probe_memory():
        for address, body in base.ranges_from_byte_map(state["undo"]):
            uc.mem_write(address, body)

    def run_candidate(pause: dict, encoded: int, mode: str) -> dict:
        uc.context_restore(pause["context"])
        uc.mem_write(base.INP + pause["offset"], bytes([encoded]))
        state["mode"] = mode
        state["reads"] = 0
        state["first_write"] = None
        state["next_read"] = None
        state["next_first_write"] = None
        state["undo"] = {}
        ended = False
        try:
            uc.emu_start(pause["rip"], base.SENT, count=5_000_000)
            ended = uc.reg_read(UC_X86_REG_RIP) == base.SENT
        except UcError:
            pass
        return {
            "encoded": encoded,
            "first_write": state["first_write"],
            "next": state["next_read"],
            "next_first_write": state["next_first_write"],
            "ended": ended,
        }

    tables: dict[int, dict] = {}
    operations: list[dict] = []
    wire = bytearray()
    pending: list[int] = []
    pending_table = None
    started = time.time()

    try:
        base.prepare_call(
            uc,
            base.BASE + base.DECODER_RVA,
            base.OBJ,
            base.CUR,
            base.INP + base.INP_SIZE,
            base.CUR + 8,
        )
        rip = base.BASE + base.DECODER_RVA
        while rip != base.SENT:
            state["mode"] = "walk"
            state["pause"] = None
            uc.emu_start(rip, base.SENT, count=100_000_000)
            rip = uc.reg_read(UC_X86_REG_RIP)
            pause = state["pause"]
            if pause is None:
                break

            rva = pause["rva"]
            if pending:
                if pending_table is None or pending_table["rva"] != rva:
                    raise RuntimeError(
                        f"varint continuation moved from {pending_table['rva']:#x} to {rva:#x}"
                    )
                token = pending.pop(0)
                encoded = pending_table["inverse"][token]
            else:
                table = tables.get(rva)
                if table is None:
                    rows = []
                    for encoded_byte in range(256):
                        row = run_candidate(pause, encoded_byte, "probe")
                        rows.append(row)
                        restore_probe_memory()
                    direct = base.build_inverse(rows, "direct")
                    varint = base.build_inverse(rows, "varint")
                    if direct is not None and varint is None:
                        kind, inverse = "direct", direct
                    elif varint is not None and direct is None:
                        kind, inverse = "varint", varint
                    elif direct is not None and varint is not None:
                        kind, inverse = "direct", direct
                    else:
                        writes = [row["first_write"] for row in rows if row["first_write"]]
                        objects = sorted(
                            {
                                (row["first_write"]["obj"], row["first_write"]["size"])
                                for row in rows
                                if row["first_write"]
                            }
                        )
                        next_sites = sorted(
                            {
                                row["next"]["rva"]
                                for row in rows
                                if row["next"]
                            }
                        )
                        distinct_values = len(
                            {
                                row["first_write"]["value"]
                                for row in rows
                                if row["first_write"]
                            }
                        )
                        raise RuntimeError(
                            f"cannot classify read site {rva:#x}: "
                            f"writes={len(writes)} objects={objects[:8]} "
                            f"distinct={distinct_values} next={next_sites[:8]}"
                        )
                    first = next(
                        (row["first_write"] for row in rows if row["first_write"]),
                        None,
                    )
                    table = {
                        "rva": rva,
                        "kind": kind,
                        "inverse": [inverse[index] for index in range(256)],
                        "write_size": first["size"] if first else 0,
                        "obj": first["obj"] if first else None,
                    }
                    tables[rva] = table
                    if len(tables) <= 5 or len(tables) % 20 == 0:
                        print(
                            f"table={len(tables)} rva={rva:#x} kind={kind} "
                            f"wire={len(wire)} elapsed={time.time()-started:.1f}s",
                            flush=True,
                        )

                baseline = export_raw()
                probe_token = 1
                probe = run_candidate(pause, table["inverse"][probe_token], "map")
                candidate_raw = export_raw()
                ranges = changed_ranges(baseline, candidate_raw)
                restore_probe_memory()

                raw_offset = None
                width = 0
                target = 0
                if ranges:
                    raw_offset = ranges[0][0]
                    if len(ranges) > 1:
                        primary = next(
                            (
                                (begin, end)
                                for begin, end in ranges
                                if int.from_bytes(
                                    candidate_raw[begin:end], "little"
                                )
                                == probe_token
                            ),
                            ranges[0],
                        )
                        raw_offset = primary[0]
                        width = primary[1] - primary[0]
                        target = int.from_bytes(
                            expected_raw[primary[0] : primary[1]], "little"
                        )
                    else:
                        width = table["write_size"]
                        if table["kind"] == "direct":
                            width = 1
                    if width not in (1, 2, 4, 8):
                        width = ranges[0][1] - ranges[0][0]
                    if len(ranges) == 1:
                        target = int.from_bytes(
                            expected_raw[raw_offset : raw_offset + width], "little"
                        )
                override = TARGET_OVERRIDE_OFFSETS.get(rva)
                if override is not None and not (
                    rva == 0x39952D and len(ranges) == 1
                ):
                    raw_offset, width = override
                    target = int.from_bytes(
                        expected_raw[raw_offset : raw_offset + width],
                        "little",
                    )
                elif rva == 0x3AAEE8 and ranges:
                    raw_offset = ranges[0][0] + 4
                    width = 4
                    target = int.from_bytes(
                        expected_raw[raw_offset : raw_offset + width],
                        "little",
                    )

                tokens = (
                    [target & 0xFF]
                    if table["kind"] == "direct"
                    else base.uleb_tokens(target)
                )
                operations.append(
                    {
                        "rva": rva,
                        "kind": table["kind"],
                        "raw": raw_offset,
                        "size": width,
                        "value": target,
                        "wire": len(wire),
                        "ranges": ranges,
                    }
                )
                pending_table = table
                token = tokens.pop(0)
                pending = tokens
                encoded = table["inverse"][token]

            uc.context_restore(pause["context"])
            uc.mem_write(base.INP + pause["offset"], bytes([encoded]))
            state["mode"] = "commit"
            state["reads"] = 0
            state["first_write"] = None
            state["next_read"] = None
            state["next_first_write"] = None
            state["undo"] = {}
            uc.emu_start(pause["rip"], base.SENT, count=100_000_000)
            rip = uc.reg_read(UC_X86_REG_RIP)
            wire.append(encoded)
            if not pending:
                pending_table = None
            if len(wire) > 0x10000:
                raise RuntimeError("wire exceeded safety limit")
    finally:
        uc.hook_del(read_handle)
        uc.hook_del(write_handle)

    result = uc.reg_read(UC_X86_REG_RAX)
    consumed = int.from_bytes(uc.mem_read(base.CUR + 8, 4), "little")
    actual_raw = export_raw()
    differences = [
        index
        for index, (actual, expected) in enumerate(zip(actual_raw, expected_raw))
        if actual != expected
    ]
    return {
        "decoder_result": result,
        "consumed": consumed,
        "wire": wire.hex(),
        "wire_size": len(wire),
        "operations": operations,
        "tables": {f"{rva:#x}": value for rva, value in sorted(tables.items())},
        "raw_exact": not differences,
        "raw_mismatch_count": len(differences),
        "raw_first_mismatch": differences[0] if differences else None,
        "actual_raw": actual_raw.hex(),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    old = args.raw.read_bytes()
    if len(old) != base.OLD_FIXED_RAW_SIZE:
        raise SystemExit(f"expected {base.OLD_FIXED_RAW_SIZE:#x} raw bytes")
    expected = old + bytes(base.FIXED_RAW_SIZE - len(old))
    result = recover(args.dll, expected)
    args.out.write_text(json.dumps(result, indent=1), encoding="utf-8")
    print(
        f"result={result['decoder_result']} consumed={result['consumed']} "
        f"wire={result['wire_size']} tables={len(result['tables'])} "
        f"raw_exact={result['raw_exact']} mismatch={result['raw_mismatch_count']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
