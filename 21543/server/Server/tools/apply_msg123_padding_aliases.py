#!/usr/bin/env python3
"""Apply captured production padding aliases to the generated Msg123 decoder."""

from __future__ import annotations

import json
from pathlib import Path
import re


root = Path(r"F:\PacketDumpTool\21543\server\Server")
generated_path = root / "Msg123BuildCurrent.generated.inc"
aliases_path = root / "build" / "msg123-current-count-aliases.json"

data = json.loads(aliases_path.read_text(encoding="utf-8"))
conflicts = set()
for message in data["unresolved"]:
    match = re.search(r"padding(\d+) code (\d+) maps to both", message)
    if match:
        conflicts.add((int(match.group(1)), int(match.group(2))))

cases = []
for site_text, mappings in data["padding_aliases"].items():
    site = int(site_text)
    for encoded_text, count in mappings.items():
        encoded = int(encoded_text)
        if (site, encoded) in conflicts:
            continue
        cases.append((site, encoded, int(count)))
cases.sort()

function_lines = [
    "// BEGIN CURRENT PADDING ALIASES",
    "bool TryCurrentPaddingAlias(",
    "    unsigned int site,",
    "    std::uint8_t encoded,",
    "    std::uint32_t& count)",
    "{",
    "    switch ((site << 8U) | encoded)",
    "    {",
]
for site, encoded, count in cases:
    function_lines.append(
        f"    case 0x{site:02X}{encoded:02X}U: count = {count}U; return true;"
    )
function_lines.extend(
    [
        "    default: return false;",
        "    }",
        "}",
        "// END CURRENT PADDING ALIASES",
        "",
    ]
)
function_text = "\n".join(function_lines)

text = generated_path.read_text(encoding="ascii")
text = re.sub(
    r"// BEGIN CURRENT PADDING ALIASES.*?// END CURRENT PADDING ALIASES\n*",
    "",
    text,
    flags=re.DOTALL,
)
text = text.replace("class Reader", function_text + "class Reader", 1)
text = text.replace(
    "bool ReadPadding(const std::array<Code, N>& codes, Byte marker)",
    "bool ReadPadding(const std::array<Code, N>& codes, Byte marker, unsigned int site)",
)
text = re.sub(
    r"r\.ReadPadding\(kPadding(\d+),\s*(\d+)\)",
    lambda match: (
        f"r.ReadPadding(kPadding{match.group(1)}, {match.group(2)}, "
        f"{match.group(1)}U)"
    ),
    text,
)
generated_path.write_text(text, encoding="ascii", newline="\n")
print(f"applied padding aliases={len(cases)} conflicts_skipped={len(conflicts)}")
