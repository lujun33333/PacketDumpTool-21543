#!/usr/bin/env python3
"""Add count-table labels to generated Msg123 diagnostics."""

from pathlib import Path
import re


path = Path(r"F:\PacketDumpTool\21543\server\Server\Msg123BuildCurrent.generated.inc")
text = path.read_text(encoding="ascii")
text = text.replace(
    "bool ReadCode(const std::array<Code, N>& codes, std::uint32_t& value)",
    "bool ReadCode(const std::array<Code, N>& codes, std::uint32_t& value, const char* field)",
)
text = re.sub(
    r"r\.ReadCode\(kCount(\d+), ([^)]+)\)",
    lambda match: (
        f'r.ReadCode(kCount{match.group(1)}, {match.group(2)}, '
        f'"count{match.group(1)}")'
    ),
    text,
)
text = text.replace(
    'std::string("msg123/current unknown count code byte=") +',
    'std::string("msg123/current ") + field + " unknown count code byte=" +',
)
path.write_text(text, encoding="ascii", newline="\n")
print(
    "labeled",
    len(re.findall(r'ReadCode\(kCount\d+, [^,]+, "count\d+"\)', text)),
)
