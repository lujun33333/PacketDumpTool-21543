#!/usr/bin/env python3
"""Locate packet-hook functions in a new Network.dll by code similarity."""

from __future__ import annotations

import argparse
import collections
import difflib
import struct

import capstone
import pefile


KNOWN_TARGETS = {
    "unpack": 0x31E220,
    "pack": 0x31CA60,
    "special_decode": 0x36FAE0,
    "inner_dispatch": 0x21DD60,
    "parser_queue_dispatch": 0x3EB610,
    "serialize_body": 0x316380,
    "safe_packet_builder": 0x34B3F0,
}

KNOWN_INTERNAL_CALLS = {
    "unpack": {
        "inner_dispatch": 0x31E5B5,
        "special_decode": 0x31E629,
    },
    "pack": {
        "safe_packet_builder": 0x31CCA4,
        "serialize_body": 0x31CCE8,
    },
    "special_decode": {
        "inner_dispatch": 0x36FC7B,
    },
    "inner_dispatch": {
        "parser_queue_dispatch": 0x21DE02,
    },
}

CE97_TARGETS = {
    "unpack": 0x33E430,
    "pack": 0x33CFE0,
    "special_decode": 0x391B70,
    "inner_dispatch": 0x232F50,
    "parser_queue_dispatch": 0x413B00,
    "serialize_body": 0x334010,
    "safe_packet_builder": 0x36C650,
}

CE97_INTERNAL_CALLS = {
    "unpack": {
        "special_decode": 0x33EACC,
    },
    "pack": {
        "safe_packet_builder": 0x33D1AB,
        "serialize_body": 0x33D1E0,
    },
    "inner_dispatch": {
        "parser_queue_dispatch": 0x233023,
    },
}


def load_image(path: str):
    pe = pefile.PE(path, fast_load=False)
    image = pe.get_memory_mapped_image()
    text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    functions = []
    entries = getattr(pe, "DIRECTORY_ENTRY_EXCEPTION", [])
    for entry in entries:
        record = getattr(entry, "struct", entry)
        begin = int(record.BeginAddress)
        end = int(record.EndAddress)
        if begin < end:
            functions.append((begin, end))
    return pe, image, text.VirtualAddress, text.Misc_VirtualSize, functions


def operand_token(insn, operand):
    if operand.type == capstone.x86.X86_OP_REG:
        return "r"
    if operand.type == capstone.x86.X86_OP_IMM:
        value = operand.imm
        if -16 <= value <= 16:
            return f"i{value}"
        return "i"
    if operand.type == capstone.x86.X86_OP_MEM:
        base = insn.reg_name(operand.mem.base)
        index = insn.reg_name(operand.mem.index)
        if base == "rip":
            return "m[rip]"
        parts = ["m["]
        if base:
            parts.append("r")
        if index:
            parts.append("+r")
        if operand.mem.disp:
            parts.append("+d")
        parts.append("]")
        return "".join(parts)
    return "?"


def instruction_tokens(image: bytes, begin: int, end: int):
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    decoder.detail = True
    result = []
    for insn in decoder.disasm(image[begin:end], begin):
        operands = ",".join(operand_token(insn, op) for op in insn.operands)
        result.append(f"{insn.mnemonic}:{operands}")
    return result


def instructions(image: bytes, begin: int, end: int):
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    decoder.detail = True
    result = []
    for insn in decoder.disasm(image[begin:end], begin):
        operands = ",".join(operand_token(insn, op) for op in insn.operands)
        result.append((insn.address, f"{insn.mnemonic}:{operands}", insn))
    return result


def map_internal_call(old_insns, new_insns, old_call_rva):
    old_index = next(
        (i for i, (address, _, _) in enumerate(old_insns) if address == old_call_rva),
        None,
    )
    if old_index is None:
        return []
    radius = 18
    old_start = max(0, old_index - radius)
    old_end = min(len(old_insns), old_index + radius + 1)
    old_window = [token for _, token, _ in old_insns[old_start:old_end]]
    relative_index = old_index - old_start
    matches = []
    for new_index, (_, _, insn) in enumerate(new_insns):
        if insn.mnemonic != "call":
            continue
        new_start = max(0, new_index - relative_index)
        new_end = min(len(new_insns), new_start + len(old_window))
        new_window = [token for _, token, _ in new_insns[new_start:new_end]]
        score = difflib.SequenceMatcher(
            None, old_window, new_window, autojunk=False
        ).ratio()
        matches.append((score, new_insns[new_index][0], insn.op_str))
    matches.sort(reverse=True)
    return matches[:5]


def ngrams(tokens, size=4):
    return collections.Counter(tuple(tokens[i : i + size]) for i in range(len(tokens) - size + 1))


def similarity(reference, candidate):
    ref_ngrams = ngrams(reference)
    candidate_ngrams = ngrams(candidate)
    overlap = sum((ref_ngrams & candidate_ngrams).values())
    ngram_score = overlap / max(1, sum(ref_ngrams.values()))
    prefix_score = difflib.SequenceMatcher(
        None, reference[:120], candidate[:120], autojunk=False
    ).ratio()
    return 0.75 * ngram_score + 0.25 * prefix_score


def direct_calls(image: bytes, text_rva: int, text_size: int, target_rva: int):
    calls = []
    end = min(len(image), text_rva + text_size)
    for offset in range(text_rva, end - 5):
        if image[offset] != 0xE8:
            continue
        displacement = struct.unpack_from("<i", image, offset + 1)[0]
        if offset + 5 + displacement == target_rva:
            calls.append(offset)
    return calls


def find_function(functions, rva):
    for begin, end in functions:
        if begin <= rva < end:
            return begin, end
    raise ValueError(f"RVA 0x{rva:X} not found in exception directory")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("known")
    parser.add_argument("current")
    parser.add_argument("--top", type=int, default=5)
    parser.add_argument("--profile", choices=("8fd", "ce97"), default="8fd")
    args = parser.parse_args()

    known_targets = CE97_TARGETS if args.profile == "ce97" else KNOWN_TARGETS
    known_internal_calls = (
        CE97_INTERNAL_CALLS if args.profile == "ce97" else KNOWN_INTERNAL_CALLS
    )
    _, old_image, _, _, old_functions = load_image(args.known)
    _, new_image, new_text_rva, new_text_size, new_functions = load_image(args.current)

    print(f"new_functions={len(new_functions)} text=0x{new_text_rva:X}+0x{new_text_size:X}")
    new_function_tokens = []
    for new_begin, new_end in new_functions:
        candidate = instruction_tokens(new_image, new_begin, new_end)
        if candidate:
            new_function_tokens.append((new_begin, new_end, candidate))

    for name, old_rva in known_targets.items():
        old_begin, old_end = find_function(old_functions, old_rva)
        old_insns = instructions(old_image, old_begin, old_end)
        reference = [token for _, token, _ in old_insns]
        scored = []
        for new_begin, new_end, candidate in new_function_tokens:
            length_ratio = min(len(reference), len(candidate)) / max(
                len(reference), len(candidate)
            )
            if length_ratio < 0.35:
                continue
            score = similarity(reference, candidate)
            scored.append((score, new_begin, new_end, len(candidate)))
        scored.sort(reverse=True)
        print(
            f"\n{name}: old=0x{old_begin:X}-0x{old_end:X} insns={len(reference)}"
        )
        for score, begin, end, count in scored[: args.top]:
            calls = direct_calls(new_image, new_text_rva, new_text_size, begin)
            call_text = ",".join(f"0x{x:X}" for x in calls) or "-"
            print(
                f"  score={score:.4f} new=0x{begin:X}-0x{end:X} "
                f"insns={count} calls={call_text}"
            )
        if scored and name in known_internal_calls:
            _, best_begin, best_end, _ = scored[0]
            new_insns = instructions(new_image, best_begin, best_end)
            for target_name, old_call_rva in known_internal_calls[name].items():
                mapped = map_internal_call(old_insns, new_insns, old_call_rva)
                mapped_text = "; ".join(
                    f"{score:.3f}@0x{address:X}->{operand}"
                    for score, address, operand in mapped
                )
                print(
                    f"  map {target_name} old_call=0x{old_call_rva:X}: "
                    f"{mapped_text or '-'}"
                )


if __name__ == "__main__":
    main()
