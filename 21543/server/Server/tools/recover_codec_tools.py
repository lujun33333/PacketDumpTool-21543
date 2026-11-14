#!/usr/bin/env python3
"""Recover deleted local codec tools from their recorded edit transcript."""

from __future__ import annotations

import json
import pathlib
import re


TRANSCRIPTS = pathlib.Path(
    r"C:\Users\lp\.cursor\projects\f-PacketDumpTool\agent-transcripts"
)
OUTPUT = pathlib.Path(__file__).resolve().parent


def load_messages(identifier: str):
    path = TRANSCRIPTS / identifier / f"{identifier}.jsonl"
    result = []
    for line in path.read_text(encoding="utf-8").splitlines():
        try:
            result.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return result


def tool_calls(messages):
    for message in messages:
        for content in message.get("message", {}).get("content", []):
            if isinstance(content, dict) and content.get("type") == "tool_use":
                yield content


def add_file_contents(patch: str, name: str) -> str | None:
    marker = f"*** Add File: "
    lines = patch.splitlines()
    collecting = False
    body = []
    for line in lines:
        if line.startswith(marker):
            collecting = line[len(marker) :].endswith(name)
            continue
        if collecting and line.startswith("*** "):
            break
        if collecting and line.startswith("+"):
            body.append(line[1:])
    return "\n".join(body) + "\n" if body else None


def apply_update(text: str, patch: str, name: str) -> str:
    if f"*** Update File:" not in patch or name not in patch:
        return text
    section = patch.split(f"*** Update File:", 1)[1]
    section = section.split("\n", 1)[1]
    section = section.split("*** End Patch", 1)[0]
    for hunk in re.split(r"^@@.*$", section, flags=re.MULTILINE):
        lines = [line for line in hunk.splitlines() if line]
        old_lines = [line[1:] for line in lines if line[0] in " -"]
        new_lines = [line[1:] for line in lines if line[0] in " +"]
        if not old_lines:
            continue
        old = "\n".join(old_lines)
        new = "\n".join(new_lines)
        if old not in text:
            raise RuntimeError(f"unable to replay patch for {name}: {old[:100]!r}")
        text = text.replace(old, new, 1)
    return text


def recover_base():
    old_id = "d8ebca76-5861-442e-b34c-3b4a09491548"
    newer_id = "c0874a52-ebc6-47f7-acc1-6c0ea7b387a5"
    text = None
    for call in tool_calls(load_messages(old_id)):
        data = call.get("input", {})
        if (
            call.get("name") == "Write"
            and isinstance(data, dict)
            and str(data.get("path", "")).endswith("extract_21669_codec.py")
        ):
            text = data["contents"]
    if text is None:
        raise RuntimeError("base extractor source not found")
    for call in tool_calls(load_messages(old_id)):
        if call.get("name") == "ApplyPatch":
            text = apply_update(text, str(call.get("input", "")), "extract_21669_codec.py")
    text = text.replace("CTOR_RVA = 0x3465E0", "CTOR_RVA = 0x347180")
    text = text.replace("DECODER_RVA = 0x392140", "DECODER_RVA = 0x3927C0")
    text = text.replace("EXPORTER_RVA = 0x36C250", "EXPORTER_RVA = 0x36C980")
    (OUTPUT / "extract_current_509_codec.py").write_text(text, encoding="utf-8")


def recover_generic():
    identifier = "c0874a52-ebc6-47f7-acc1-6c0ea7b387a5"
    text = None
    calls = list(tool_calls(load_messages(identifier)))
    for call in calls:
        if call.get("name") != "ApplyPatch":
            continue
        patch = str(call.get("input", ""))
        added = add_file_contents(patch, "extract509generic.py")
        if added is not None:
            text = added
            continue
        if text is not None:
            text = apply_update(text, patch, "extract509generic.py")
    if text is None:
        raise RuntimeError("generic extractor source not found")
    text = text.replace("import extract_21669_codec as base", "import extract_current_509_codec as base")
    (OUTPUT / "extract_current_509_generic.py").write_text(text, encoding="utf-8")


def recover_generator():
    identifier = "c0874a52-ebc6-47f7-acc1-6c0ea7b387a5"
    text = None
    calls = list(tool_calls(load_messages(identifier)))
    for call in calls:
        if call.get("name") != "ApplyPatch":
            continue
        patch = str(call.get("input", ""))
        added = add_file_contents(patch, "generate509cpp.py")
        if added is not None:
            text = added
            continue
        if text is not None:
            text = apply_update(text, patch, "generate509cpp.py")
    if text is None:
        raise RuntimeError("509 generator source not found")
    (OUTPUT / "generate_current_509_cpp.py").write_text(text, encoding="utf-8")


def recover_msg123_generator():
    identifier = "c0874a52-ebc6-47f7-acc1-6c0ea7b387a5"
    text = None
    for call in tool_calls(load_messages(identifier)):
        if call.get("name") != "ApplyPatch":
            continue
        patch = str(call.get("input", ""))
        added = add_file_contents(patch, "msg123generic.py")
        if added is not None:
            text = added
            continue
        if text is not None:
            text = apply_update(text, patch, "msg123generic.py")
    if text is None:
        raise RuntimeError("Msg123 generator source not found")
    replacements = {
        'DLL = Path("F:\\\\QQ飞车 - 副本\\\\Releasephysx27_64\\\\Network.dll")':
            'DLL = Path(r"F:\\\\QQ飞车\\\\Releasephysx27_64\\\\Network.dll")',
        "CTOR, COMPRESS, WRITE, DTOR = 0x34C940, 0x387400, 0x3DE190, 0x368D80":
            "CTOR, COMPRESS, WRITE, DTOR = 0x34D4E0, 0x387B30, 0x3DE810, 0x36C220",
        "OBJ_SIZE, PLAIN = 0xA03A0, 42213":
            "OBJ_SIZE, PLAIN = 0x200000, 42213",
        "RAND_IAT = 0x4AB008":
            "RAND_IAT = 0x4B5058",
        'STRUCT = json.loads(Path("struct123.json").read_text(encoding="utf-8"))':
            'STRUCT = json.loads(Path(r"F:\\\\PacketDumpTool\\\\21543\\\\server\\\\Server\\\\build\\\\msg123-current-structure.json").read_text(encoding="utf-8"))',
        "QQSpeedMsg123Build21669":
            "QQSpeedMsg123BuildCurrent",
        "Generated from the 21669 Network.dll":
            "Generated from the current Network.dll",
        '"Msg123Build21669.generated.inc"':
            '"Msg123BuildCurrent.generated.inc"',
    }
    for old, new in replacements.items():
        text = text.replace(old, new)
    (OUTPUT / "generate_current_msg123_decoder.py").write_text(
        text,
        encoding="utf-8",
    )


if __name__ == "__main__":
    recover_base()
    recover_generic()
    recover_generator()
    print("recovered current 509 codec tools")
