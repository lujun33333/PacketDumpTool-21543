"""Recover the QQSpeed 21669 Msg509 safe-codec tables.

Port of the 21529 recovery: the client decoder is emulated under Unicorn and driven one
input byte at a time. At every unseen read site all 256 encoded bytes are probed to
recover the byte bijection. The 21529 recovery output supplies field order and the plain
object offsets; the target object is a real 43779-byte capture extended with the 300
zero bytes 21669 appended.

Unlike the 21529 script the codec kind is measured from the probe rows instead of being
taken from the old schema, so a field whose width changed between builds is reported
rather than silently mis-encoded.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import pefile
from unicorn import (
    UC_ARCH_X86,
    UC_HOOK_MEM_INVALID,
    UC_HOOK_MEM_READ,
    UC_HOOK_MEM_WRITE,
    UC_MODE_64,
    UC_PROT_ALL,
    Uc,
    UcError,
)
from unicorn.x86_const import *

BASE = 0x180000000
PAGE = 0x1000
STACK = 0x7000000000
STACK_SIZE = 0x200000
OBJ = 0x7100000000
OBJ_SIZE = 0x100000
INP = 0x7200000000
INP_SIZE = 0x100000
CUR = 0x7300000000
OUT = 0x7400000000
OUT_SIZE = 0x200000
SENT = 0x7500000000

# 21669 Network.dll, CSSafeCNotifyGameBeginPara
CTOR_RVA = 0x347180
DECODER_RVA = 0x3927C0
EXPORTER_RVA = 0x36C980
SAFE_OBJECT_SIZE = 0x81EF8
FIXED_RAW_SIZE = 0xAC2F
OLD_FIXED_RAW_SIZE = 0xAB03


def align(value: int, unit: int = PAGE) -> int:
    return (value + unit - 1) & ~(unit - 1)


def new_unicorn(dll: Path) -> Uc:
    pe = pefile.PE(str(dll), fast_load=False)
    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    uc.mem_map(BASE, align(pe.OPTIONAL_HEADER.SizeOfImage), UC_PROT_ALL)
    raw = dll.read_bytes()
    uc.mem_write(BASE, raw[: pe.OPTIONAL_HEADER.SizeOfHeaders])
    for section in pe.sections:
        data = section.get_data()
        if data:
            uc.mem_write(BASE + section.VirtualAddress, data)
    for address, size in (
        (STACK, STACK_SIZE),
        (OBJ, OBJ_SIZE),
        (INP, INP_SIZE),
        (CUR, PAGE),
        (OUT, OUT_SIZE),
        (SENT, PAGE),
    ):
        uc.mem_map(address, align(size), UC_PROT_ALL)

    def invalid(emu, access, address, size, value, user):
        rip = emu.reg_read(UC_X86_REG_RIP)
        print(
            f"invalid access={access} address={address:#x} size={size} rip={rip:#x}",
            file=sys.stderr,
        )
        return False

    uc.hook_add(UC_HOOK_MEM_INVALID, invalid)
    return uc


def prepare_call(uc: Uc, address: int, rcx: int, rdx=0, r8=0, r9=0) -> None:
    rsp = (STACK + STACK_SIZE - 0x1000) & ~0xF
    rsp -= 8
    uc.mem_write(rsp, SENT.to_bytes(8, "little"))
    uc.reg_write(UC_X86_REG_RSP, rsp)
    uc.reg_write(UC_X86_REG_RCX, rcx)
    uc.reg_write(UC_X86_REG_RDX, rdx)
    uc.reg_write(UC_X86_REG_R8, r8)
    uc.reg_write(UC_X86_REG_R9, r9)
    uc.reg_write(UC_X86_REG_RIP, address)


def call(uc: Uc, address: int, rcx: int, rdx=0, r8=0, r9=0, maxins=100_000_000) -> int:
    prepare_call(uc, address, rcx, rdx, r8, r9)
    uc.emu_start(address, SENT, count=maxins)
    return uc.reg_read(UC_X86_REG_RAX)


def ranges_from_byte_map(values: dict[int, int]) -> list[tuple[int, bytes]]:
    if not values:
        return []
    keys = sorted(values)
    output: list[tuple[int, bytes]] = []
    start = previous = keys[0]
    body = bytearray([values[start]])
    for key in keys[1:]:
        if key == previous + 1:
            body.append(values[key])
        else:
            output.append((start, bytes(body)))
            start = key
            body = bytearray([values[key]])
        previous = key
    output.append((start, bytes(body)))
    return output


def uleb_tokens(value: int) -> list[int]:
    output = []
    while True:
        token = value & 0x7F
        value >>= 7
        if value:
            token |= 0x80
        output.append(token)
        if not value:
            return output


def build_inverse(rows: list[dict], kind: str) -> dict[int, int] | None:
    """Map plain token -> encoded wire byte, or None when the kind does not fit."""
    inverse: dict[int, int] = {}
    for row in rows:
        first = row.get("first_write")
        if first is None:
            continue
        if kind == "direct":
            token = first["value"] & 0xFF
        else:
            following = row.get("next_first_write")
            same = bool(following) and following["obj"] == first["obj"]
            token = (first["value"] & 0x7F) | (0x80 if same else 0)
        if token in inverse and inverse[token] != row["encoded"]:
            return None
        inverse[token] = row["encoded"]
    return inverse if set(inverse) == set(range(256)) else None


def load_schema(recovery_path: Path, new_raw: bytes) -> list[dict]:
    data = json.loads(recovery_path.read_text(encoding="utf-8"))
    operations = []
    for op in data["semantic_ops"]:
        raw_offset = op.get("raw")
        if raw_offset is None:
            value = op["value"]
        else:
            value = int.from_bytes(new_raw[raw_offset : raw_offset + op["size"]], "little")
        operations.append(
            {
                "source_op": op["source_op"],
                "old_rva": op.get("new_rva"),
                "kind": op["kind"],
                "size": op["size"],
                "raw": raw_offset,
                "value": value,
            }
        )
    return operations


def recover(dll: Path, operations: list[dict], expected_raw: bytes, limit: int) -> dict:
    uc = new_unicorn(dll)
    uc.mem_write(OBJ, b"\0" * OBJ_SIZE)
    ctor_result = call(uc, BASE + CTOR_RVA, OBJ)
    if ctor_result != OBJ:
        raise RuntimeError(f"constructor returned {ctor_result:#x}")

    uc.mem_write(INP, b"\0" * INP_SIZE)
    uc.mem_write(CUR, INP.to_bytes(8, "little"))
    uc.mem_write(CUR + 8, (0).to_bytes(4, "little"))
    prepare_call(uc, BASE + DECODER_RVA, OBJ, CUR, INP + INP_SIZE, CUR + 8)

    state = {
        "mode": "main",
        "pause": None,
        "probe_reads": 0,
        "first_write": None,
        "next": None,
        "next_first_write": None,
        "undo": {},
    }
    tables: dict[int, dict] = {}
    safe = bytearray()
    trace: list[dict] = []
    started = time.time()

    def read_hook(emu, access, address, size, value, user):
        if not (INP <= address < INP + INP_SIZE):
            return
        rva = emu.reg_read(UC_X86_REG_RIP) - BASE
        offset = address - INP
        if state["mode"] == "main":
            state["pause"] = {
                "rva": rva,
                "offset": offset,
                "context": emu.context_save(),
                "rip": emu.reg_read(UC_X86_REG_RIP),
            }
            emu.emu_stop()
        elif state["mode"] == "commit":
            state["mode"] = "main"
        elif state["mode"] == "probe":
            state["probe_reads"] += 1
            if state["probe_reads"] >= 2:
                state["next"] = {"rva": rva, "offset": offset}

    def write_hook(emu, access, address, size, value, user):
        if state["mode"] != "probe":
            return
        for index in range(size):
            target = address + index
            if target not in state["undo"]:
                try:
                    state["undo"][target] = emu.mem_read(target, 1)[0]
                except UcError:
                    pass
        if OBJ <= address < OBJ + OBJ_SIZE:
            record = {
                "obj": address - OBJ,
                "size": size,
                "value": value,
                "rva": emu.reg_read(UC_X86_REG_RIP) - BASE,
            }
            if state["probe_reads"] == 1 and state["first_write"] is None:
                state["first_write"] = record
            elif state["probe_reads"] >= 2 and state["next_first_write"] is None:
                state["next_first_write"] = record
                emu.emu_stop()

    read_handle = uc.hook_add(UC_HOOK_MEM_READ, read_hook)
    write_handle = uc.hook_add(UC_HOOK_MEM_WRITE, write_hook)
    rip = BASE + DECODER_RVA
    op_index = 0
    pending: list[int] = []
    mismatches: list[dict] = []

    try:
        while rip != SENT:
            if state["mode"] != "commit":
                state["mode"] = "main"
            state["pause"] = None
            uc.emu_start(rip, SENT, count=100_000_000)
            rip = uc.reg_read(UC_X86_REG_RIP)
            pause = state["pause"]
            if pause is None:
                break
            if op_index >= len(operations):
                trace.append({"event": "extra-byte", "rva": pause["rva"], "offset": pause["offset"]})
                break
            if pause["offset"] != len(safe):
                raise RuntimeError(
                    f"non-sequential input offset {pause['offset']} != {len(safe)}"
                )

            rva = pause["rva"]
            operation = operations[op_index]

            if rva not in tables:
                rows = []
                for encoded in range(256):
                    uc.context_restore(pause["context"])
                    uc.mem_write(INP + pause["offset"], bytes([encoded]))
                    state["mode"] = "probe"
                    state["probe_reads"] = 0
                    state["first_write"] = None
                    state["next"] = None
                    state["next_first_write"] = None
                    state["undo"] = {}
                    try:
                        uc.emu_start(pause["rip"], SENT, count=2_000_000)
                    except UcError:
                        pass
                    rows.append(
                        {
                            "encoded": encoded,
                            "first_write": state["first_write"],
                            "next": state["next"],
                            "next_first_write": state["next_first_write"],
                        }
                    )
                    for address, body in ranges_from_byte_map(state["undo"]):
                        uc.mem_write(address, body)

                direct = build_inverse(rows, "direct")
                varint = build_inverse(rows, "varint")
                if direct is not None and varint is None:
                    kind = "direct"
                    inverse = direct
                elif varint is not None and direct is None:
                    kind = "varint"
                    inverse = varint
                elif direct is not None and varint is not None:
                    kind = operation["kind"]
                    inverse = direct if kind == "direct" else varint
                else:
                    raise RuntimeError(
                        f"no consistent table at rva={rva:#x} (op {op_index}); "
                        f"writes={sum(1 for r in rows if r['first_write'])}/256"
                    )

                if kind != operation["kind"]:
                    mismatches.append(
                        {
                            "op_index": op_index,
                            "rva": rva,
                            "schema_kind": operation["kind"],
                            "measured_kind": kind,
                            "obj": rows[0]["first_write"]["obj"] if rows[0]["first_write"] else None,
                        }
                    )
                    print(
                        f"!! op {op_index}: schema says {operation['kind']}, "
                        f"decoder behaves as {kind} at rva={rva:#x}",
                        flush=True,
                    )

                tables[rva] = {"rva": rva, "kind": kind, "inverse": [inverse[i] for i in range(256)]}
                if len(tables) % 10 == 0 or len(tables) < 5:
                    print(
                        f"table {len(tables):3d} rva={rva:#x} kind={kind:<7} "
                        f"op={op_index}/{len(operations)} wire={len(safe)} "
                        f"elapsed={time.time()-started:.1f}s",
                        flush=True,
                    )

            table = tables[rva]
            if not pending:
                if table["kind"] == "direct":
                    if not 0 <= operation["value"] <= 0xFF:
                        raise RuntimeError(
                            f"op {op_index} value {operation['value']} does not fit a direct field"
                        )
                    pending = [operation["value"] & 0xFF]
                else:
                    pending = uleb_tokens(operation["value"])

            token = pending.pop(0)
            chosen = table["inverse"][token]
            uc.context_restore(pause["context"])
            uc.mem_write(INP + pause["offset"], bytes([chosen]))
            safe.append(chosen)
            state["mode"] = "commit"
            rip = pause["rip"]
            if not pending:
                op_index += 1
            if limit and len(safe) >= limit:
                trace.append({"event": "limit", "wire": len(safe)})
                break
    finally:
        uc.hook_del(read_handle)
        uc.hook_del(write_handle)

    decoder_result = uc.reg_read(UC_X86_REG_RAX)
    cursor = int.from_bytes(uc.mem_read(CUR + 8, 4), "little")
    summary = {
        "ops_used": op_index,
        "ops_total": len(operations),
        "safe_length": len(safe),
        "decoder_result": decoder_result,
        "decoder_cursor": cursor,
        "reached_sentinel": rip == SENT,
        "table_count": len(tables),
        "mismatches": mismatches,
        "trace": trace,
    }

    if rip == SENT and decoder_result == 1 and cursor == len(safe):
        uc.mem_write(OUT, b"\0" * OUT_SIZE)
        summary["exporter_result"] = call(uc, BASE + EXPORTER_RVA, OBJ, OUT)
        exported = bytes(uc.mem_read(OUT, FIXED_RAW_SIZE))
        diff = [i for i, (a, b) in enumerate(zip(exported, expected_raw)) if a != b]
        summary["raw_exact"] = not diff
        summary["raw_mismatch_count"] = len(diff)
        summary["raw_first_mismatch"] = diff[0] if diff else None
        summary["exported_raw_hex"] = exported.hex()

    summary["tables"] = {f"{rva:#x}": table for rva, table in sorted(tables.items())}
    summary["safe_hex"] = safe.hex()
    return summary


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", type=Path, required=True)
    ap.add_argument("--recovery", type=Path, required=True)
    ap.add_argument("--old-raw", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()

    old_raw = args.old_raw.read_bytes()
    if len(old_raw) != OLD_FIXED_RAW_SIZE:
        raise SystemExit(f"old raw must be {OLD_FIXED_RAW_SIZE:#x}, got {len(old_raw):#x}")
    new_raw = bytearray(FIXED_RAW_SIZE)
    new_raw[:OLD_FIXED_RAW_SIZE] = old_raw

    operations = load_schema(args.recovery, bytes(new_raw))
    print(f"schema ops={len(operations)} target_raw={FIXED_RAW_SIZE:#x}", flush=True)

    summary = recover(args.dll, operations, bytes(new_raw), args.limit)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(summary, indent=1), encoding="utf-8")

    print()
    print(f"ops_used         : {summary['ops_used']}/{summary['ops_total']}")
    print(f"safe_length      : {summary['safe_length']}")
    print(f"tables           : {summary['table_count']}")
    print(f"decoder_result   : {summary['decoder_result']}")
    print(f"decoder_cursor   : {summary['decoder_cursor']}")
    print(f"reached_sentinel : {summary['reached_sentinel']}")
    print(f"kind mismatches  : {len(summary['mismatches'])}")
    if "raw_exact" in summary:
        print(f"raw_exact        : {summary['raw_exact']} (mismatch={summary.get('raw_mismatch_count')})")
    print(f"out              : {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
