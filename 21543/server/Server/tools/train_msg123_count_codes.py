#!/usr/bin/env python3
"""Learn production count-code aliases from captured Msg123 plain/wire pairs."""

from __future__ import annotations

import argparse
import difflib
import json
import pathlib
import re

from msg123patch import PinnedEncoder


COUNT_FIELDS = {
    0: 3039,
    1: 42208,
    2: 13440,
    3: 3002,
    4: 24,
    5: 329,
    6: 471,
    7: 13055,
    8: 3000,
    9: 1895,
    10: 13408,
    11: 40157,
    12: 11043,
    13: 13446,
    14: 13501,
    15: 845,
    16: 609,
    17: 654,
    18: 564,
    19: 13170,
    20: 13422,
    21: 40162,
    22: 40179,
}


def first_diff(left: bytes, right: bytes) -> int:
    for index, values in enumerate(zip(left, right)):
        if values[0] != values[1]:
            return index
    return min(len(left), len(right))


def actual_position(deterministic: bytes, actual: bytes, index: int) -> int | None:
    matcher = difflib.SequenceMatcher(
        a=deterministic,
        b=actual,
        autojunk=False,
    )
    for tag, left_begin, left_end, right_begin, right_end in matcher.get_opcodes():
        if left_begin <= index < left_end:
            if tag == "equal":
                return right_begin + index - left_begin
            if left_end - left_begin == 1:
                return right_begin
            return None
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dll", type=pathlib.Path, required=True)
    parser.add_argument("--payloads", type=pathlib.Path, required=True)
    parser.add_argument("--generated", type=pathlib.Path, required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    args = parser.parse_args()

    encoder = PinnedEncoder(
        args.dll,
        0x34D4E0,
        0x387B30,
        0x3DE810,
        0x36C220,
        0x200000,
        42213,
        0x4B5058,
    )
    encoder.rand_value = 0
    aliases = {str(index): {} for index in COUNT_FIELDS}
    generated = args.generated.read_text(encoding="ascii")
    padding_key_to_ids = {}
    padding_bodies = {
        int(index): body
        for index, body in re.findall(
            r"kPadding(\d+) = \{\{(.*?)\}\};",
            generated,
            re.DOTALL,
        )
    }
    padding_markers = {}
    for index, marker in re.findall(
        r"ReadPadding\(kPadding(\d+),\s*(\d+)\)",
        generated,
    ):
        padding_markers.setdefault(int(index), set()).add(int(marker))
    for index, body in padding_bodies.items():
        match = re.search(
            r"Code\{\s*1,\s*(\d+),\s*0,\s*1\s*\}",
            body,
        )
        if not match:
            continue
        code = int(match.group(1))
        for marker in padding_markers.get(index, set()):
            padding_key_to_ids.setdefault((code, marker), []).append(index)
    padding_aliases = {
        str(index): {}
        for index in padding_bodies
    }
    unresolved = []

    for source_path in sorted(args.payloads.glob("*_safe123_plain_source_*.bin")):
        match = re.search(r"_(\d+)_safe123_plain_source_", source_path.name)
        if not match:
            continue
        sequence = int(match.group(1))
        payload_matches = list(
            args.payloads.glob(
                f"*_{sequence + 1}_safe28346_builder_payload_full_*.bin"
            )
        )
        if len(payload_matches) != 1:
            unresolved.append(f"{sequence}: paired safe payload missing")
            continue

        plain = source_path.read_bytes()
        full = payload_matches[0].read_bytes()
        inner_length = int.from_bytes(full[10:12], "little")
        actual = full[12 : 12 + inner_length]
        deterministic = encoder.encode(plain)
        opcodes = difflib.SequenceMatcher(
            a=deterministic,
            b=actual,
            autojunk=False,
        ).get_opcodes()
        for opcode_index, opcode in enumerate(opcodes[:-1]):
            tag, left_begin, left_end, right_begin, right_end = opcode
            following = opcodes[opcode_index + 1]
            if not (
                tag == "replace"
                and left_end - left_begin == 1
                and right_end > right_begin
                and following[0] == "equal"
                and following[1] == left_end
                and following[3] == right_end
                and left_end < len(deterministic)
            ):
                continue
            key = (deterministic[left_begin], deterministic[left_end])
            padding_ids = padding_key_to_ids.get(key, [])
            if len(padding_ids) != 1:
                continue
            padding_id = str(padding_ids[0])
            encoded = str(actual[right_begin])
            count = right_end - right_begin
            previous = padding_aliases[padding_id].get(encoded)
            if previous is not None and previous != count:
                unresolved.append(
                    f"{sequence}: padding{padding_id} code {encoded} "
                    f"maps to both {previous} and {count}"
                )
            else:
                padding_aliases[padding_id][encoded] = count

        for count_id, offset in COUNT_FIELDS.items():
            value = plain[offset]
            changed = bytearray(plain)
            changed[offset] = value + 1 if value < 0xFF else 0
            changed_wire = encoder.encode(bytes(changed))
            index = first_diff(deterministic, changed_wire)
            position = actual_position(deterministic, actual, index)
            if position is None or position >= len(actual):
                unresolved.append(
                    f"{sequence}: count{count_id} position unresolved"
                )
                continue
            encoded = actual[position]
            previous = aliases[str(count_id)].get(str(encoded))
            if previous is not None and previous != value:
                unresolved.append(
                    f"{sequence}: count{count_id} code {encoded} "
                    f"maps to both {previous} and {value}"
                )
                continue
            aliases[str(count_id)][str(encoded)] = value

    args.out.write_text(
        json.dumps(
            {
                "aliases": aliases,
                "padding_aliases": padding_aliases,
                "unresolved": unresolved,
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    print(
        "aliases",
        sum(len(values) for values in aliases.values()),
        "unresolved",
        len(unresolved),
    )
    for count_id, values in aliases.items():
        if values:
            print(f"count{count_id}={values}")
    print(
        "padding_aliases",
        sum(len(values) for values in padding_aliases.values()),
    )
    for item in unresolved[:20]:
        print(f"unresolved={item}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
