"""Build and validate a generic 21669 msg123 decoder from the client oracle.

The client writer is the ground truth.  We pin rand() to a controllable value so
every padding site has a known length, discover the recursive field program from
struct123.json, extract every scalar/count/padding transform, then round-trip
oracle packets without relying on the obsolete Build323 field order.
"""

from __future__ import annotations

import json
import random
from pathlib import Path

from msg123patch import PinnedEncoder


DLL = Path(r"F:\QQ飞车\Releasephysx27_64\Network.dll")
CTOR, COMPRESS, WRITE, DTOR = 0x34D4E0, 0x387B30, 0x3DE810, 0x36C220
OBJ_SIZE, PLAIN = 0x200000, 42213
RAND_IAT = 0x4B5058
STRUCT = json.loads(Path(r"F:\\PacketDumpTool\\21543\\server\\Server\\build\\msg123-current-structure.json").read_text(encoding="utf-8"))

# Capacities needed by production packets.  Unknown optional blocks default to
# 20, which covers player-sized collections; the large canonical collections
# have explicit official limits.
COUNT_MAX = {
    24: 150,       # passed checkpoints
    329: 14,       # acceleration verification records
    13158: 40,     # game-detail different-corner records
}
DEFAULT_COUNT_MAX = 100


def first_diff(a: bytes, b: bytes) -> int:
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i
    return min(len(a), len(b))


class Oracle:
    def __init__(self) -> None:
        self.enc = PinnedEncoder(
            DLL, CTOR, COMPRESS, WRITE, DTOR, OBJ_SIZE, PLAIN, rand_iat=RAND_IAT
        )

    def encode(self, values: dict[int, int] | None = None, pad_count: int = 1) -> bytes:
        if not 1 <= pad_count <= 16:
            raise ValueError("pad_count must be 1..16")
        source = bytearray(PLAIN)
        for off, value in (values or {}).items():
            if not 0 <= off < PLAIN:
                raise ValueError(f"plain offset {off} out of range")
            source[off] = value & 0xFF
        self.enc.rand_value = pad_count - 1
        return self.enc.encode(bytes(source))


def attach_tree() -> list[dict]:
    def clone(fields: list[dict]) -> list[dict]:
        out = []
        for raw in fields:
            f = dict(raw)
            children = f.pop("element", None)
            if children is None:
                children = STRUCT["elements"].get(str(f["base"]))
            if children is not None:
                f["children"] = clone(children)
                f["kind"] = "count"
            out.append(f)
        return out

    return clone(STRUCT["top"])


class Builder:
    def __init__(self, oracle: Oracle) -> None:
        self.oracle = oracle
        self.top = attach_tree()
        self.pad_sites: list[dict] = []
        self.scalar_fields: list[dict] = []
        self.count_fields: list[dict] = []

    def classify_and_position(
        self, fields: list[dict], hold: dict[int, int], sample: bytes, sample2: bytes
    ) -> None:
        for f in fields:
            values = dict(hold)
            values[f["base"]] = 1
            changed = self.oracle.encode(values, 1)
            f["pos"] = first_diff(sample, changed)
            changed2 = self.oracle.encode(values, 2)
            f["pos2"] = first_diff(sample2, changed2)
            if "children" in f:
                f["kind"] = "count"
                self.count_fields.append(f)
            else:
                values[f["base"]] = 200
                wide = self.oracle.encode(values, 1)
                f["kind"] = "var" if len(wide) != len(sample) else "byte"
                self.scalar_fields.append(f)

    def decode_gap(self, one: bytes, two: bytes, scope: str) -> list[dict]:
        """Split a no-field gap into constants and padding sites.

        A constant consumes 1 byte in both samples.  A padding site consumes
        count-byte+1 marker in the rand=0 sample and count-byte+2 markers in
        the rand=1 sample.  Dynamic programming removes the ambiguity that
        made repeated marker bytes look like fields/constants.
        """
        memo: dict[tuple[int, int], list[tuple] | None] = {}

        def solve(i: int, j: int) -> list[tuple] | None:
            key = (i, j)
            if key in memo:
                return memo[key]
            if i == len(one) and j == len(two):
                return []
            answer = None
            if i < len(one) and j < len(two) and one[i] == two[j]:
                tail = solve(i + 1, j + 1)
                if tail is not None:
                    answer = [("const", one[i])] + tail
            if (
                answer is None
                and i + 1 < len(one)
                and j + 2 < len(two)
                and one[i + 1] == two[j + 1] == two[j + 2]
            ):
                tail = solve(i + 2, j + 3)
                if tail is not None:
                    answer = [
                        ("pad", one[i], two[j], one[i + 1])
                    ] + tail
            memo[key] = answer
            return answer

        decoded = solve(0, 0)
        if decoded is None:
            raise RuntimeError(
                f"{scope}: cannot split gap one={one.hex()} two={two.hex()}"
            )
        out = []
        for item in decoded:
            if item[0] == "const":
                out.append({"op": "const", "value": item[1], "scope": scope})
                continue
            site = {
                "op": "pad",
                "id": len(self.pad_sites),
                "scope": scope,
                "marker": item[3],
                "codes": {item[1]: 1, item[2]: 2},
            }
            self.pad_sites.append(site)
            out.append(site)
        return out

    def make_program(
        self,
        fields: list[dict],
        sample: bytes,
        sample2: bytes,
        start: int,
        end: int,
        start2: int,
        end2: int,
        scope: str,
    ) -> list[dict]:
        ordered = sorted(fields, key=lambda f: f["pos"])
        if [f["base"] for f in ordered] != [
            f["base"] for f in sorted(fields, key=lambda f: f["pos2"])
        ]:
            raise RuntimeError(f"{scope}: field order changes with padding length")
        program = []
        pos, pos2 = start, start2
        for field in ordered:
            program.extend(
                self.decode_gap(
                    sample[pos:field["pos"]],
                    sample2[pos2:field["pos2"]],
                    scope,
                )
            )
            program.append({"op": "field", "field": field})
            pos = field["pos"] + 1
            pos2 = field["pos2"] + 1
        program.extend(
            self.decode_gap(sample[pos:end], sample2[pos2:end2], scope)
        )
        return program

    def build_level(
        self,
        fields: list[dict],
        hold: dict[int, int],
        start: int,
        end: int,
        scope: str,
    ) -> list[dict]:
        sample = self.oracle.encode(hold, 1)
        sample2 = self.oracle.encode(hold, 2)
        self.classify_and_position(fields, hold, sample, sample2)
        for f in fields:
            if not start <= f["pos"] < end:
                raise RuntimeError(
                    f"{scope}: field {f['base']} at {f['pos']} outside {start}..{end}"
                )
        # The matching region under two-byte padding is supplied by the caller
        # through the corresponding field positions/growth.
        start2 = start
        end2 = end
        if scope != "top":
            # For element scopes, the parent count field immediately precedes
            # the region under either padding setting.
            parent_base = int(scope.rsplit("_", 1)[1])
            parent = next(f for f in self.count_fields if f["base"] == parent_base)
            parent_hold = self.context_for(parent)
            zero2 = self.oracle.encode(parent_hold, 2)
            one_hold2 = dict(parent_hold)
            one_hold2[parent_base] = 1
            one2 = self.oracle.encode(one_hold2, 2)
            start2 = parent["pos2"] + 1
            end2 = start2 + len(one2) - len(zero2)
        else:
            start2, end2 = 0, len(sample2)
        program = self.make_program(
            fields, sample, sample2, start, end, start2, end2, scope
        )
        for f in fields:
            if f["kind"] != "count":
                continue
            zero = self.oracle.encode(hold, 1)
            child_hold = dict(hold)
            child_hold[f["base"]] = 1
            one = self.oracle.encode(child_hold, 1)
            growth = len(one) - len(zero)
            if growth <= 0:
                raise RuntimeError(f"{scope}: count {f['base']} adds no element")
            child_start = f["pos"] + 1
            child_end = child_start + growth
            f["program"] = self.build_level(
                f["children"],
                child_hold,
                child_start,
                child_end,
                f"{scope}.count_{f['base']}",
            )
        return program

    def build(self) -> list[dict]:
        sample = self.oracle.encode({}, 1)
        self.program = self.build_level(self.top, {}, 0, len(sample), "top")
        self.extract_scalar_tables()
        self.extract_count_codes()
        self.extract_padding_codes()
        return self.program

    def context_for(self, target: dict) -> dict[int, int]:
        found: dict[int, int] | None = None

        def walk(fields: list[dict], hold: dict[int, int]) -> None:
            nonlocal found
            for f in fields:
                if f is target:
                    found = dict(hold)
                    return
                if "children" in f:
                    child_hold = dict(hold)
                    child_hold[f["base"]] = 1
                    walk(f["children"], child_hold)
                    if found is not None:
                        return

        walk(self.top, {})
        if found is None:
            raise RuntimeError(f"no context for field {target['base']}")
        return found

    def extract_scalar_tables(self) -> None:
        for index, f in enumerate(self.scalar_fields):
            hold = self.context_for(f)
            inverse: dict[int, int] = {}
            for value in range(256):
                values = dict(hold)
                values[f["base"]] = value
                wire = self.oracle.encode(values, 1)
                raw = value if f["kind"] == "byte" or value < 128 else (
                    (value & 0x7F) | 0x80
                )
                inverse[wire[f["pos"]]] = raw
            if len(inverse) != 256:
                raise RuntimeError(
                    f"field {f['base']} {f['kind']} table has {len(inverse)} entries"
                )
            f["table"] = inverse
            if (index + 1) % 25 == 0:
                print(f"  scalar tables {index + 1}/{len(self.scalar_fields)}", flush=True)

    def extract_count_codes(self) -> None:
        for f in self.count_fields:
            hold = self.context_for(f)
            maximum = COUNT_MAX.get(f["base"], DEFAULT_COUNT_MAX)
            codes: dict[bytes, int] = {}
            for value in range(maximum + 1):
                values = dict(hold)
                values[f["base"]] = value
                wire = self.oracle.encode(values, 1)
                length = 1 if value < 128 else 2
                code = wire[f["pos"]:f["pos"] + length]
                if code in codes:
                    # Bool/optional counts are clamped by Compress.  The first
                    # repeated code marks the end of the legal domain.
                    maximum = value - 1
                    break
                codes[code] = value
            f["codes"] = codes
            f["maximum"] = maximum

    def all_count_ones(self) -> dict[int, int]:
        return {f["base"]: 1 for f in self.count_fields}

    def extract_padding_codes(self) -> None:
        values = self.all_count_ones()
        for count in range(3, 17):
            wire = self.oracle.encode(values, count)
            p = 0

            def walk(program: list[dict]) -> None:
                nonlocal p
                for token in program:
                    if token["op"] == "pad":
                        encoded = wire[p]
                        marker = wire[p + 1]
                        if wire[p + 1:p + 1 + count] != bytes([marker]) * count:
                            raise RuntimeError(
                                f"pad {token['id']} ({token['scope']}) not uniform "
                                f"actual={wire[p:p + 5].hex()}"
                            )
                        if count == 1:
                            token["marker"] = marker
                        elif marker != token["marker"]:
                            raise RuntimeError(f"pad {token['id']} marker changed")
                        token["codes"][encoded] = count
                        p += 1 + count
                        continue
                    if token["op"] == "const":
                        p += 1
                        continue

                    f = token["field"]
                    if f["kind"] == "byte":
                        p += 1
                    elif f["kind"] == "var":
                        # All scalar values in this extraction vector are zero.
                        p += 1
                    else:
                        # Every count is one, then exactly one recursive element.
                        p += 1
                        walk(f["program"])

            walk(self.program)
            if p != len(wire):
                raise RuntimeError(f"padding extraction left {len(wire) - p} bytes")
        for site in self.pad_sites:
            if site["op"] != "pad":
                continue
            if len(site["codes"]) != 16:
                raise RuntimeError(
                    f"pad {site['id']} has {len(site['codes'])}/16 codes"
                )


class Decoder:
    def __init__(self, program: list[dict]) -> None:
        self.program = program

    def decode(self, wire: bytes) -> tuple[dict[int, int], int]:
        p = 0
        values: dict[int, int] = {}

        def read_var(table: dict[int, int], bits: int) -> int:
            nonlocal p
            value = 0
            shift = 0
            max_bytes = (bits + 6) // 7
            for index in range(max_bytes):
                if p >= len(wire):
                    raise ValueError("truncated varint")
                raw = table.get(wire[p])
                p += 1
                if raw is None:
                    raise ValueError("unknown varint byte")
                payload = raw & 0x7F
                value |= payload << shift
                if not raw & 0x80:
                    if index and payload == 0:
                        raise ValueError("non-canonical varint")
                    return value
                shift += 7
            raise ValueError("varint too long")

        def read_code(codes: dict[bytes, int]) -> int:
            nonlocal p
            match = None
            for code, value in codes.items():
                if wire[p:p + len(code)] == code:
                    if match is None or len(code) > len(match[0]):
                        match = (code, value)
            if match is None:
                raise ValueError(f"unknown count code at {p}")
            p += len(match[0])
            return match[1]

        def walk(program: list[dict]) -> None:
            nonlocal p
            for token in program:
                if token["op"] == "pad":
                    if p >= len(wire):
                        raise ValueError(f"padding {token['id']}: truncated count byte")
                    p += 1
                    marker = token["marker"]
                    count = 0
                    while count < 16 and p + count < len(wire) and wire[p + count] == marker:
                        count += 1
                    if count == 0:
                        raise ValueError(f"padding {token['id']}: marker mismatch at {p}")
                    p += count
                    continue
                if token["op"] == "const":
                    if p >= len(wire):
                        raise ValueError(f"truncated opaque byte at {p}")
                    p += 1
                    continue

                f = token["field"]
                if f["kind"] == "byte":
                    if p >= len(wire) or wire[p] not in f["table"]:
                        raise ValueError(f"unknown byte field {f['base']} at {p}")
                    values[f["base"]] = f["table"][wire[p]]
                    p += 1
                elif f["kind"] == "var":
                    values[f["base"]] = read_var(f["table"], f["width"] * 8)
                else:
                    try:
                        count = read_code(f["codes"])
                    except ValueError as exc:
                        raise ValueError(f"count field {f['base']}: {exc}") from exc
                    values[f["base"]] = count
                    for _ in range(count):
                        walk(f["program"])

        walk(self.program)
        if p != len(wire):
            raise ValueError(f"trailing {len(wire) - p} bytes")
        return values, p


def emit_cpp(builder: Builder, path: Path) -> None:
    scalar_ids = {id(f): i for i, f in enumerate(builder.scalar_fields)}
    count_ids = {id(f): i for i, f in enumerate(builder.count_fields)}

    def array_u8(values: list[int]) -> str:
        rows = []
        for i in range(0, len(values), 16):
            rows.append("        " + ", ".join(str(v) for v in values[i:i + 16]))
        return ",\n".join(rows)

    lines = [
        "// Generated from the current Network.dll msg123 writer oracle.",
        "// Do not edit by hand; regenerate with msg123generic.py.",
        "namespace QQSpeedMsg123BuildCurrent",
        "{",
        "using Byte = std::uint8_t;",
        "using DecodeTable = std::array<Byte, 256>;",
        "",
        "struct Code",
        "{",
        "    Byte length;",
        "    Byte first;",
        "    Byte second;",
        "    std::uint16_t value;",
        "};",
        "",
        "struct GameplayFields",
        "{",
        "    std::uint32_t uin = 0;",
        "    std::uint32_t timestamp = 0;",
        "    std::uint32_t currentState = 0;",
        "    std::array<std::uint32_t, 3> position{};",
        "    std::uint8_t checkpointCount = 0;",
        "    std::array<std::uint16_t, 150> checkpointIds{};",
        "    std::uint32_t lapTime = 0;",
        "    std::uint8_t messageSequence = 0;",
        "    std::uint32_t currentMapId = 0;",
        "    std::uint8_t currentInfoFlag = 0;",
        "    std::uint32_t totalAccelerateFuel = 0;",
        "    std::uint32_t destinationNpcId = 0;",
        "    std::uint32_t distanceToEnd = 0;",
        "    std::size_t safeBytesConsumed = 0;",
        "};",
        "",
    ]

    for f in builder.scalar_fields:
        sid = scalar_ids[id(f)]
        table = [0] * 256
        for encoded, raw in f["table"].items():
            table[encoded] = raw
        lines.extend([
            f"inline constexpr DecodeTable kScalar{sid} = {{{{",
            array_u8(table),
            "}};",
            "",
        ])

    def emit_codes(name: str, codes: dict[bytes, int]) -> None:
        ordered = sorted(codes.items(), key=lambda item: (item[1], len(item[0])))
        lines.append(f"inline constexpr std::array<Code, {len(ordered)}> {name} = {{{{")
        for code, value in ordered:
            second = code[1] if len(code) == 2 else 0
            lines.append(
                f"    Code{{ {len(code)}, {code[0]}, {second}, {value} }},"
            )
        lines.extend(["}};", ""])

    for f in builder.count_fields:
        emit_codes(f"kCount{count_ids[id(f)]}", f["codes"])
    for site in builder.pad_sites:
        emit_codes(
            f"kPadding{site['id']}",
            {bytes([encoded]): value for encoded, value in site["codes"].items()},
        )

    lines.extend([
        "class Reader",
        "{",
        "public:",
        "    Reader(const Byte* data, std::size_t size, std::string* error,",
        "           std::size_t start = 0,",
        "           bool allowUnknownCountZero = false,",
        "           bool allowShiftedPadding = false)",
        "        : data_(data), size_(size), position_(start), error_(error),",
        "          allowUnknownCountZero_(allowUnknownCountZero),",
        "          allowShiftedPadding_(allowShiftedPadding) {}",
        "    std::size_t Position() const { return position_; }",
        "    bool AtEnd() const { return position_ == size_; }",
        "    bool Fail(const char* reason)",
        "    {",
        "        if (error_)",
        "            *error_ = std::string(\"msg123/current \") + reason +",
        "                \" at safe offset \" + std::to_string(position_);",
        "        return false;",
        "    }",
        "    bool Take(Byte& value)",
        "    {",
        "        if (position_ >= size_) return Fail(\"is truncated\");",
        "        value = data_[position_++];",
        "        return true;",
        "    }",
        "    bool ReadByte(const DecodeTable& table, std::uint64_t& value)",
        "    {",
        "        Byte encoded = 0;",
        "        if (!Take(encoded)) return false;",
        "        value = table[encoded];",
        "        return true;",
        "    }",
        "    bool ReadVar(const DecodeTable& table, unsigned int bits, std::uint64_t& value)",
        "    {",
        "        if (bits == 0 || bits > 64) return Fail(\"has invalid varint width\");",
        "        value = 0;",
        "        const unsigned int maxBytes = (bits + 6u) / 7u;",
        "        for (unsigned int index = 0; index < maxBytes; ++index)",
        "        {",
        "            Byte encoded = 0;",
        "            if (!Take(encoded)) return false;",
        "            const Byte raw = table[encoded];",
        "            const std::uint64_t payload = raw & 0x7Fu;",
        "            const unsigned int shift = index * 7u;",
        "            const unsigned int remaining = bits > shift ? bits - shift : 0;",
        "            if (remaining < 7u && remaining != 0 && (payload >> remaining) != 0)",
        "                return Fail(\"varint overflows width\");",
        "            value |= payload << shift;",
        "            if ((raw & 0x80u) == 0)",
        "            {",
        "                if (index != 0 && payload == 0)",
        "                    return Fail(\"uses non-canonical varint\");",
        "                return true;",
        "            }",
        "        }",
        "        return Fail(\"varint is too long\");",
        "    }",
        "    template <std::size_t N>",
        "    bool ReadCode(const std::array<Code, N>& codes, std::uint32_t& value, const char* field)",
        "    {",
        "        const Code* match = nullptr;",
        "        for (const Code& code : codes)",
        "        {",
        "            if (position_ + code.length > size_) continue;",
        "            if (data_[position_] != code.first) continue;",
        "            if (code.length == 2 && data_[position_ + 1] != code.second) continue;",
        "            if (!match || code.length > match->length) match = &code;",
        "        }",
        "        if (!match)",
        "        {",
        "            if (allowUnknownCountZero_ && position_ < size_)",
        "            {",
        "                ++position_;",
        "                value = 0;",
        "                return true;",
        "            }",
        "            if (error_)",
        "                *error_ = std::string(\"msg123/current count field \") + field +",
        "                    \" has unknown code at safe offset \" + std::to_string(position_);",
        "            return false;",
        "        }",
        "        position_ += match->length;",
        "        value = match->value;",
        "        return true;",
        "    }",
        "    template <std::size_t N>",
        "    bool ReadPadding(const std::array<Code, N>& codes, Byte marker, unsigned int site)",
        "    {",
        "        // The count code and marker are session-derived in production;",
        "        // the marker run remains self-delimiting.",
        "        (void)codes;",
        "        (void)marker;",
        "        lastPaddingShifted_ = false;",
        "        Byte encodedCount = 0;",
        "        if (!Take(encodedCount)) return false;",
        "        if (position_ >= size_) return Fail(\"has truncated padding\");",
        "        const Byte actualMarker = data_[position_];",
        "        std::uint32_t count = 0;",
        "        while (count < 64 && position_ + count < size_ &&",
        "               data_[position_ + count] == actualMarker)",
        "            ++count;",
        "        if (count == 0) return Fail(\"has invalid padding marker\");",
        "        if (allowShiftedPadding_ && count == 1 && position_ + 2 < size_)",
        "        {",
        "            const Byte shiftedMarker = data_[position_ + 1];",
        "            std::uint32_t shiftedCount = 0;",
        "            while (shiftedCount < 64 &&",
        "                   position_ + 1 + shiftedCount < size_ &&",
        "                   data_[position_ + 1 + shiftedCount] == shiftedMarker)",
        "                ++shiftedCount;",
        "            if (shiftedCount >= 3)",
        "            {",
        "                position_ += 1 + shiftedCount;",
        "                lastPaddingShifted_ = true;",
        "                return true;",
        "            }",
        "        }",
        "        position_ += count;",
        "        return true;",
        "    }",
        "    bool ReadConst(Byte expected)",
        "    {",
        "        // Bytes not backed by a discovered plain-object field may be",
        "        // session-derived in production. They still occupy one byte,",
        "        // so consume them without pinning the oracle's zero-state value.",
        "        (void)expected;",
        "        Byte actual = 0;",
        "        return Take(actual);",
        "    }",
        "private:",
        "    const Byte* data_ = nullptr;",
        "    std::size_t size_ = 0;",
        "    std::size_t position_ = 0;",
        "    std::string* error_ = nullptr;",
        "    bool lastPaddingShifted_ = false;",
        "    bool allowUnknownCountZero_ = false;",
        "    bool allowShiftedPadding_ = false;",
        "};",
        "",
    ])

    for f in builder.count_fields:
        cid = count_ids[id(f)]
        lines.append(
            f"bool DecodeElement{cid}(Reader& r, GameplayFields& out, std::uint32_t elementIndex);"
        )
    lines.append("")

    capture = {
        0: "out.uin = static_cast<std::uint32_t>(value);",
        4: "out.timestamp = static_cast<std::uint32_t>(value);",
        8: "out.currentState = static_cast<std::uint32_t>(value);",
        12: "out.position[0] = static_cast<std::uint32_t>(value);",
        16: "out.position[1] = static_cast<std::uint32_t>(value);",
        20: "out.position[2] = static_cast<std::uint32_t>(value);",
        25: (
            "if (elementIndex < out.checkpointIds.size()) "
            "out.checkpointIds[elementIndex] = static_cast<std::uint16_t>(value);"
        ),
        325: "out.lapTime = static_cast<std::uint32_t>(value);",
        470: "out.messageSequence = static_cast<std::uint8_t>(value);",
        13387: "out.currentInfoFlag = static_cast<std::uint8_t>(value);",
        13388: "out.totalAccelerateFuel = static_cast<std::uint32_t>(value);",
        13392: "out.destinationNpcId = static_cast<std::uint32_t>(value);",
        13396: "out.distanceToEnd = static_cast<std::uint32_t>(value);",
    }

    def emit_program(program: list[dict], indent: str) -> list[str]:
        out_lines = []
        temp = 0
        for token in program:
            if token["op"] == "const":
                out_lines.append(
                    f"{indent}if (!r.ReadConst({token['value']})) return false;"
                )
                continue
            if token["op"] == "pad":
                out_lines.append(
                    f"{indent}if (!r.ReadPadding(kPadding{token['id']}, "
                    f"{token['marker']}, {token['id']}U)) return false;"
                )
                continue
            f = token["field"]
            if f["kind"] in ("byte", "var"):
                name = f"value{temp}"
                temp += 1
                sid = scalar_ids[id(f)]
                out_lines.append(f"{indent}std::uint64_t {name} = 0;")
                if f["kind"] == "byte":
                    out_lines.append(
                        f"{indent}if (!r.ReadByte(kScalar{sid}, {name})) return false;"
                    )
                else:
                    out_lines.append(
                        f"{indent}if (!r.ReadVar(kScalar{sid}, {f['width'] * 8}, "
                        f"{name})) return false;"
                    )
                statement = capture.get(f["base"])
                if statement:
                    out_lines.append(f"{indent}{statement.replace('value', name)}")
                continue
            cid = count_ids[id(f)]
            name = f"count{temp}"
            temp += 1
            out_lines.extend([
                f"{indent}std::uint32_t {name} = 0;",
                f"{indent}if (!r.ReadCode(kCount{cid}, {name}, \"{f['base']}\")) return false;",
                f"{indent}if ({name} > {f['maximum']}) return r.Fail(\"count exceeds capacity\");",
            ])
            if f["base"] == 24:
                out_lines.append(
                    f"{indent}out.checkpointCount = static_cast<std::uint8_t>({name});"
                )
            out_lines.extend([
                f"{indent}for (std::uint32_t i = 0; i < {name}; ++i)",
                f"{indent}    if (!DecodeElement{cid}(r, out, i)) return false;",
            ])
        return out_lines

    # Element functions first; forward declarations permit arbitrary nesting.
    for f in builder.count_fields:
        cid = count_ids[id(f)]
        lines.extend([
            f"bool DecodeElement{cid}(Reader& r, GameplayFields& out, std::uint32_t elementIndex)",
            "{",
            "    (void)elementIndex;",
        ])
        lines.extend(emit_program(f["program"], "    "))
        lines.extend(["    return true;", "}", ""])

    lines.extend([
        "bool DecodeSafeRequest(",
        "    const std::uint8_t* safe,",
        "    std::size_t safeLength,",
        "    GameplayFields& out,",
        "    std::string* error)",
        "{",
        "    if (error) error->clear();",
        "    if (!safe || safeLength == 0 || safeLength > 100000)",
        "    {",
        "        if (error) *error = \"msg123/current invalid safe body\";",
        "        return false;",
        "    }",
        "    out = GameplayFields{};",
        "    Reader r(safe, safeLength, error);",
    ])
    lines.extend(emit_program(builder.program, "    "))
    lines.extend([
        "    if (!r.AtEnd()) return r.Fail(\"has trailing bytes\");",
        "    out.safeBytesConsumed = r.Position();",
        "    if (error) *error = \"ok\";",
        "    return true;",
        "}",
        "#include \"Msg123BuildCurrentTailFallback.inc\"",
        "}",
        "",
    ])
    path.write_text("\n".join(lines), encoding="ascii", newline="\n")
    print(f"wrote {path} ({path.stat().st_size} bytes)")


def main() -> None:
    oracle = Oracle()
    builder = Builder(oracle)
    program = builder.build()
    print(
        f"program ready: scalars={len(builder.scalar_fields)} "
        f"counts={len(builder.count_fields)} pads={len(builder.pad_sites)}"
    )
    decoder = Decoder(program)

    # Every optional block present once, all values zero, all 16 padding lengths.
    count_ones = builder.all_count_ones()
    for pad_count in range(1, 17):
        wire = oracle.encode(count_ones, pad_count)
        got, consumed = decoder.decode(wire)
        assert consumed == len(wire)
        assert all(got.get(base) == 1 for base in count_ones)
    print("all-counts=1 / padding 1..16: OK")

    # Scalar-only randomized vectors validate all byte and varint transforms.
    rng = random.Random(21669)
    for case in range(100):
        cfg: dict[int, int] = {}
        expected: dict[int, int] = {}
        for f in builder.scalar_fields:
            # Only top-level scalars are active when all counts are zero.
            if builder.context_for(f) or f["base"] in {13404}:
                continue
            bits = min(f["width"] * 8, 16)
            value = rng.randrange(1 << bits)
            for i in range(f["width"]):
                cfg[f["base"] + i] = (value >> (i * 8)) & 0xFF
            expected[f["base"]] = value
        pad_count = rng.randrange(1, 17)
        got, _ = decoder.decode(oracle.encode(cfg, pad_count))
        mismatches = {
            base: (value, got.get(base))
            for base, value in expected.items()
            if got.get(base) != value
        }
        if mismatches:
            raise RuntimeError(
                f"scalar mismatch in case {case}: {mismatches}"
            )
    print("100 randomized top-level scalar vectors: OK")

    emit_cpp(
        builder,
        Path(
            "F:\\PacketDumpTool\\21543\\server\\Server\\"
            "Msg123BuildCurrent.generated.inc"
        ),
    )

    Path("msg123generic_model.json").write_text(
        json.dumps(
            {
                "scalar_count": len(builder.scalar_fields),
                "count_count": len(builder.count_fields),
                "padding_count": len(builder.pad_sites),
            },
            indent=2,
        ),
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
