from __future__ import annotations

import argparse
import difflib
import json
from pathlib import Path


def trace_paths(root: Path):
    yield "Aurora", 1, root / "safe-aurora-1p.current.json"
    yield "CarGod", 1, root / "safe-cargod-1p.current.json"
    for count in range(1, 7):
        yield "Border", count, root / f"safe-border-{count}p.current.json"
    for count in range(1, 13):
        yield "Custom", count, root / f"safe-custom-{count}p.current.json"
    for count in range(1, 7):
        yield "NormalCurrent", count, root / f"safe-normal-{count}p.current.json"
    for count in range(1, 7):
        yield "NormalAnonymous", count, root / f"safe-anonymous-{count}p.current.json"
    for count in range(1, 7):
        yield "Ranked", count, root / f"safe-ranked-{count}p.current.json"


def chunks(values, size=16):
    for index in range(0, len(values), size):
        yield values[index : index + size]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    traces = []
    table_by_rva = {}
    for profile, count, path in trace_paths(args.root):
        data = json.loads(path.read_text(encoding="utf-8"))
        allowed_opaque_mismatch = (
            data.get("raw_mismatch_count") == 1 and
            data.get("raw_first_mismatch") == 0x758
        )
        if (not data["raw_exact"] and not allowed_opaque_mismatch) or data["decoder_result"] != 1:
            raise RuntimeError(f"unvalidated trace: {path}")
        traces.append((profile, count, data))
        for key, table in data["tables"].items():
            rva = int(key, 0)
            signature = (table["kind"], tuple(table["inverse"]))
            previous = table_by_rva.get(rva)
            if previous is not None and previous != signature:
                raise RuntimeError(f"conflicting table at RVA {rva:#x}")
            table_by_rva[rva] = signature

    extra_paths = {
        "mapCheck": args.root / "safe-normal-mapcheck-1p.current.json",
        "speed": args.root / "safe-normal-speed-1p.current.json",
        "combined": args.root / "safe-normal-mapcheck-speed-1p.current.json",
    }
    extra_data = {}
    for name, path in extra_paths.items():
        data = json.loads(path.read_text(encoding="utf-8"))
        if data["decoder_result"] != 1:
            raise RuntimeError(f"unvalidated telemetry trace: {path}")
        extra_data[name] = data
        for key, table in data["tables"].items():
            rva = int(key, 0)
            signature = (table["kind"], tuple(table["inverse"]))
            previous = table_by_rva.get(rva)
            if previous is not None and previous != signature:
                raise RuntimeError(f"conflicting telemetry table at RVA {rva:#x}")
            table_by_rva[rva] = signature

    normal_one = next(
        data for profile, count, data in traces
        if profile == "NormalCurrent" and count == 1
    )

    def operation_key(operation):
        return operation["rva"], operation["raw"], operation["size"]

    def inserted_operations(variant):
        base_operations = normal_one["operations"]
        variant_operations = variant["operations"]
        matcher = difflib.SequenceMatcher(
            a=[operation_key(operation) for operation in base_operations],
            b=[operation_key(operation) for operation in variant_operations],
            autojunk=False,
        )
        inserted = []
        for tag, left_begin, left_end, right_begin, right_end in matcher.get_opcodes():
            if tag == "insert":
                inserted.extend(variant_operations[right_begin:right_end])
            elif tag != "equal":
                raise RuntimeError(
                    f"telemetry trace is not a pure insertion: {tag} "
                    f"{left_begin}:{left_end} -> {right_begin}:{right_end}"
                )
        return inserted

    map_check_operations = inserted_operations(extra_data["mapCheck"])
    speed_insertions = inserted_operations(extra_data["speed"])
    normal_speed_operations = [
        operation for operation in speed_insertions
        if operation["raw"] is not None and 0x630 <= operation["raw"] <= 0x63E
    ]
    speed_operations = [
        dict(operation, size=2)
        if operation["raw"] == 0x629 else operation
        for operation in speed_insertions
        if operation["raw"] is not None and 0x61B <= operation["raw"] <= 0x629
    ]
    if (
        len(map_check_operations) != 15 or
        len(normal_speed_operations) != 8 or
        len(speed_operations) != 8
    ):
        raise RuntimeError("unexpected telemetry insertion shape")

    rvas = sorted(table_by_rva)
    table_index = {rva: index for index, rva in enumerate(rvas)}
    lines = [
        "// Generated from QQSpeed current Network.dll client-decoder oracles.",
        "// Every profile trace was decoded and expanded byte-exact before export.",
        "namespace qqspeed::buildCurrentNormal {",
        "namespace {",
        "enum class CodecKind : std::uint8_t { Direct, Varint };",
        "struct CodecTable {",
        "    CodecKind kind;",
        "    std::array<std::uint8_t, 256> inverse;",
        "};",
        "struct Operation {",
        "    std::uint16_t codec;",
        "    std::uint16_t rawOffset;",
        "    std::uint8_t size;",
        "};",
        "constexpr std::uint16_t kConstantZero = 0xFFFFU;",
        f"constexpr std::array<CodecTable, {len(rvas)}> kCodecTables = {{{{",
    ]
    for rva in rvas:
        kind, inverse = table_by_rva[rva]
        lines.append(f"    // 21669 read RVA {rva:#x}")
        lines.append(
            "    { CodecKind::"
            + ("Direct" if kind == "direct" else "Varint")
            + ", {{"
        )
        for group in chunks(inverse):
            lines.append(
                "        "
                + ",".join(f"0x{value:02X}" for value in group)
                + ","
            )
        lines.append("    }} },")
    lines.append("}};")

    descriptors = []
    for profile, count, data in traces:
        name = f"kTrace{profile}{count}"
        operations = data["operations"]
        lines.append(
            f"constexpr std::array<Operation, {len(operations)}> {name} = {{{{"
        )
        for operation in operations:
            rva = operation["rva"]
            raw = (
                "kConstantZero"
                if operation["raw"] is None
                else f"0x{operation['raw']:04X}U"
            )
            lines.append(
                f"    {{ {table_index[rva]}U, {raw}, {operation['size']}U }},"
            )
        lines.append("}};")
        descriptors.append((profile, count, name))

    def emit_operation_array(name, operations):
        lines.append(
            f"constexpr std::array<Operation, {len(operations)}> {name} = {{{{"
        )
        for operation in operations:
            raw = (
                "kConstantZero"
                if operation["raw"] is None
                else f"0x{operation['raw']:04X}U"
            )
            lines.append(
                f"    {{ {table_index[operation['rva']]}U, {raw}, "
                f"{operation['size']}U }},"
            )
        lines.append("}};")

    emit_operation_array("kMapCheckOperations", map_check_operations)
    emit_operation_array("kSpeedOperations", speed_operations)
    emit_operation_array("kNormalSpeedOperations", normal_speed_operations)

    lines.extend(
        [
            "struct TraceView { const Operation* data; std::size_t size; };",
            "template <std::size_t N>",
            "TraceView View(const std::array<Operation, N>& operations) {",
            "    return { operations.data(), operations.size() };",
            "}",
            "bool SelectTrace(",
            "    const std::uint8_t* raw,",
            "    std::size_t rawSize,",
            "    TraceView& trace,",
            "    std::string* error) {",
            "    if (!raw || rawSize < 0xA7C4U) {",
            "        if (error) *error = \"current Msg509 fixed object is truncated\";",
            "        return false;",
            "    }",
            "    const auto readU16 = [raw](std::size_t offset) {",
            "        return static_cast<std::uint16_t>(raw[offset]) |",
            "            static_cast<std::uint16_t>(raw[offset + 1]) << 8U;",
            "    };",
            "    const std::size_t players = raw[0x796U];",
            "    const std::uint16_t baseMode = readU16(0x8E65U);",
            "    const std::uint16_t subMode = readU16(0x8E67U);",
            "    const std::size_t anonymousCount = raw[0x9B0DU];",
            "    if (baseMode == 10U && subMode == 1U && players == 1U) {",
            "        trace = View(kTraceCarGod1);",
            "        return true;",
            "    }",
            "    if (baseMode == 42U && subMode == 29U && players == 1U) {",
            "        trace = View(kTraceAurora1);",
            "        return true;",
            "    }",
            "    if (baseMode == 42U && subMode == 27U) {",
            "        switch (players) {",
        ]
    )
    for count in range(1, 13):
        lines.append(
            f"        case {count}U: trace = View(kTraceCustom{count}); return true;"
        )
    lines.extend(
        [
            "        default: break;",
            "        }",
            "    }",
            "    if (baseMode == 40U && subMode == 1U) {",
            "        switch (players) {",
        ]
    )
    for count in range(1, 7):
        lines.append(
            f"        case {count}U: trace = View(kTraceRanked{count}); return true;"
        )
    lines.extend(
        [
            "        default: break;",
            "        }",
            "    }",
            "    if (baseMode == 1U && subMode == 1U) {",
            "        if (anonymousCount != 0U) {",
            "            if (anonymousCount != players) {",
            "                if (error) *error = \"current Msg509 anonymous/player count mismatch\";",
            "                return false;",
            "            }",
            "            switch (players) {",
        ]
    )
    for count in range(1, 7):
        lines.append(
            f"            case {count}U: trace = View(kTraceNormalAnonymous{count}); return true;"
        )
    lines.extend(
        [
            "            default: break;",
            "            }",
            "        } else if (raw[0x74BU] != 0U) {",
            "            switch (players) {",
        ]
    )
    for count in range(1, 7):
        lines.append(
            f"            case {count}U: trace = View(kTraceNormalCurrent{count}); return true;"
        )
    lines.extend(
        [
            "            default: break;",
            "            }",
            "        } else {",
            "            switch (players) {",
        ]
    )
    for count in range(1, 7):
        lines.append(
            f"            case {count}U: trace = View(kTraceBorder{count}); return true;"
        )
    lines.extend(
        [
            "            default: break;",
            "            }",
            "        }",
            "    }",
            "    if (error) *error = \"unsupported current Msg509 mode/player profile\";",
            "    return false;",
            "}",
            "std::uint64_t ReadValue(",
            "    const std::uint8_t* raw,",
            "    std::size_t rawSize,",
            "    const Operation& operation) {",
            "    if (operation.rawOffset == kConstantZero) return 0U;",
            "    std::uint64_t value = 0U;",
            "    for (std::size_t index = 0; index < operation.size; ++index) {",
            "        const std::size_t offset =",
            "            static_cast<std::size_t>(operation.rawOffset) + index;",
            "        if (offset < rawSize)",
            "            value |= static_cast<std::uint64_t>(raw[offset]) << (index * 8U);",
            "    }",
            "    return value;",
            "}",
            "bool EmitOperation(",
            "    const std::uint8_t* raw,",
            "    std::size_t rawSize,",
            "    const Operation& operation,",
            "    std::vector<std::uint8_t>& out,",
            "    std::string* error) {",
            "    const CodecTable& codec = kCodecTables[operation.codec];",
            "    std::uint64_t value = ReadValue(raw, rawSize, operation);",
            "    if (codec.kind == CodecKind::Direct) {",
            "        if (value > 0xFFU) {",
            "            if (error) *error = \"21669 Msg509 direct field exceeds one byte\";",
            "            return false;",
            "        }",
            "        out.push_back(codec.inverse[static_cast<std::uint8_t>(value)]);",
            "        return true;",
            "    }",
            "    do {",
            "        std::uint8_t token = static_cast<std::uint8_t>(value & 0x7FU);",
            "        value >>= 7U;",
            "        if (value != 0U) token |= 0x80U;",
            "        out.push_back(codec.inverse[token]);",
            "    } while (value != 0U);",
            "    return true;",
            "}",
            "} // namespace",
            "",
            "bool EncodeNotifyGameBegin509(",
            "    const std::uint8_t* raw,",
            "    std::size_t rawSize,",
            "    std::vector<std::uint8_t>& out,",
            "    std::string* error) {",
            "    out.clear();",
            "    TraceView trace{};",
            "    if (!SelectTrace(raw, rawSize, trace, error)) return false;",
            "    out.reserve(trace.size + 256U);",
            "    for (std::size_t index = 0; index < trace.size; ++index) {",
            "        const Operation& operation = trace.data[index];",
            "        if (!EmitOperation(raw, rawSize, operation, out, error)) {",
            "            out.clear();",
            "            return false;",
            "        }",
            "        if (operation.rawOffset == 0x0589U) {",
            "            const std::size_t count = raw[0x0589U];",
            "            if (count > kMapCheckOperations.size() / 3U) {",
            "                if (error) *error = \"21669 Msg509 map-check count exceeds recovered telemetry capacity\";",
            "                out.clear();",
            "                return false;",
            "            }",
            "            for (std::size_t item = 0; item < count * 3U; ++item)",
            "                if (!EmitOperation(raw, rawSize, kMapCheckOperations[item], out, error)) {",
            "                    out.clear();",
            "                    return false;",
            "                }",
            "        } else if (operation.rawOffset == 0x061AU) {",
            "            const std::size_t count = raw[0x061AU];",
            "            if (count > kSpeedOperations.size()) {",
            "                if (error) *error = \"21669 Msg509 speed-section count exceeds recovered telemetry capacity\";",
            "                out.clear();",
            "                return false;",
            "            }",
            "            for (std::size_t item = 0; item < count; ++item)",
            "                if (!EmitOperation(raw, rawSize, kSpeedOperations[item], out, error)) {",
            "                    out.clear();",
            "                    return false;",
            "                }",
            "        } else if (operation.rawOffset == 0x062FU) {",
            "            const std::size_t count = raw[0x062FU];",
            "            if (count > kNormalSpeedOperations.size()) {",
            "                if (error) *error = \"21669 Msg509 normal-speed count exceeds recovered telemetry capacity\";",
            "                out.clear();",
            "                return false;",
            "            }",
            "            for (std::size_t item = 0; item < count; ++item)",
            "                if (!EmitOperation(raw, rawSize, kNormalSpeedOperations[item], out, error)) {",
            "                    out.clear();",
            "                    return false;",
            "                }",
            "        }",
            "    }",
            "    return true;",
            "}",
            "} // namespace qqspeed::buildCurrentNormal",
            "",
        ]
    )
    args.out.write_text("\n".join(lines), encoding="ascii", newline="\n")
    print(
        f"wrote {args.out} traces={len(traces)} tables={len(rvas)} "
        f"operations={sum(len(data['operations']) for _, _, data in traces)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
