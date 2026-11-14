#!/usr/bin/env python3
"""Add scalar labels to generated Msg123 varint diagnostics."""

from pathlib import Path
import re


path = Path(r"F:\PacketDumpTool\21543\server\Server\Msg123BuildCurrent.generated.inc")
text = path.read_text(encoding="ascii")
text = text.replace(
    "bool ReadVar(const DecodeTable& table, unsigned int bits, std::uint64_t& value)",
    "bool ReadVar(const DecodeTable& table, unsigned int bits, std::uint64_t& value, const char* field)",
)
text = text.replace(
    'return Fail("varint overflows width");',
    'return Fail((std::string(field) + " varint overflows width").c_str());',
)
text = text.replace(
    'return Fail("uses non-canonical varint");',
    'return Fail((std::string(field) + " uses non-canonical varint").c_str());',
)
text = text.replace(
    'return Fail("varint is too long");',
    'return Fail((std::string(field) + " varint is too long").c_str());',
)
text = re.sub(
    r"r\.ReadVar\(kScalar(\d+),\s*(\d+),\s*([^)]+)\)",
    lambda match: (
        f'r.ReadVar(kScalar{match.group(1)}, {match.group(2)}, '
        f'{match.group(3)}, "scalar{match.group(1)}")'
    ),
    text,
)
path.write_text(text, encoding="ascii", newline="\n")
print(
    "labeled",
    len(re.findall(r'ReadVar\(kScalar\d+, \d+, [^,]+, "scalar\d+"\)', text)),
)
