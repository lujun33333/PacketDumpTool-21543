from __future__ import annotations

import argparse
import ctypes
import hashlib
import os
from dataclasses import dataclass
from pathlib import Path


OBJECT_SIZE = 0x100000


@dataclass(frozen=True)
class ParserLayout:
    label: str
    direct_compact_read_rva: int
    plain_size: int
    has_aurora_tail: bool
    network_dll_sha256: str
    safe_ctor_rva: int | None = None
    safe_read_rva: int | None = None
    safe_expand_rva: int | None = None
    safe_destructor_rva: int | None = None


PARSER_LAYOUTS = {
    # Network.dll shipped with the client that produced the latest supplied
    # capture.  Its direct CNotifyGameBegin object ends before the Aurora tail.
    "captured-21487": ParserLayout(
        "captured 21487 direct parser",
        0x1B7AA0,
        43_257,
        False,
        "d2bc986b61e8fec993c576ed0db087f5209c8f5a31db2c273efa5228b457db12",
    ),
    # Newer 21487 research binary.  Its direct parser includes the Aurora tail.
    # The safe/28346 parser is a different function at RVA 0x36FD70.
    "research-21487": ParserLayout(
        "research 21487 direct parser",
        0x1D6940,
        43_775,
        True,
        "eea666e1fa33cfa3f51e59178edf7f9ad87227a0dc1372889b3e143ba2710ba9",
        0x326490,
        0x36FD70,
        0x34B0A0,
        0x347680,
    ),
    "research-21543": ParserLayout(
        "research 21543 direct parser",
        0x1D6BC0,
        43_779,
        True,
        "5b44ee6b7f94bba0986e195c09fdbef74cb9eea3f0152b9a4a0b3d00733ceba1",
        0x326840,
        0x370550,
        0x34B610,
        0x347BF0,
    ),
}


def u16le(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little")


def u32le(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little")


class Direct509Parser:
    def __init__(
        self,
        network_dll: Path,
        layout: ParserLayout,
        dependency_directories: list[Path],
    ) -> None:
        self._layout = layout
        self._dll_directory_handles = []
        for directory in [network_dll.parent, *dependency_directories]:
            if directory.is_dir():
                self._dll_directory_handles.append(
                    os.add_dll_directory(str(directory))
                )
        module = ctypes.WinDLL(str(network_dll))
        self._module = module
        self._read = ctypes.CFUNCTYPE(
            ctypes.c_int64,
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_uint64,
        )(module._handle + layout.direct_compact_read_rva)

    def parse(self, payload_without_root_length: bytes) -> bytes:
        declared_size = len(payload_without_root_length) + 2
        if declared_size > 0x7FFF:
            raise AssertionError(
                f"direct 509 root exceeds compact limit: {declared_size}"
            )
        wire = declared_size.to_bytes(2, "big") + payload_without_root_length
        obj = ctypes.create_string_buffer(OBJECT_SIZE)
        source = ctypes.create_string_buffer(wire)
        result = self._read(
            ctypes.addressof(obj),
            ctypes.addressof(source),
            len(wire),
        )
        if result != 0:
            raise AssertionError(
                f"{self._layout.label} rejected payload: "
                f"result={result:#x}, wire_length={len(wire)}"
            )

        # A one-byte truncation must be rejected by the native parser.  This
        # guards against accidentally validating only a readable prefix.
        truncated_obj = ctypes.create_string_buffer(OBJECT_SIZE)
        truncated_source = ctypes.create_string_buffer(wire[:-1])
        truncated_result = self._read(
            ctypes.addressof(truncated_obj),
            ctypes.addressof(truncated_source),
            len(wire) - 1,
        )
        if truncated_result == 0:
            raise AssertionError(
                f"{self._layout.label} accepted a one-byte truncation"
            )
        return obj.raw[: self._layout.plain_size]


class Safe509Parser:
    def __init__(self, module: ctypes.WinDLL, layout: ParserLayout) -> None:
        if (
            layout.safe_ctor_rva is None
            or layout.safe_read_rva is None
            or layout.safe_expand_rva is None
            or layout.safe_destructor_rva is None
        ):
            raise AssertionError(f"{layout.label} has no verified safe parser layout")
        self._layout = layout
        self._ctor = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(
            module._handle + layout.safe_ctor_rva
        )
        self._read = ctypes.CFUNCTYPE(
            ctypes.c_bool,
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_uint64),
            ctypes.c_uint64,
            ctypes.POINTER(ctypes.c_uint32),
        )(module._handle + layout.safe_read_rva)
        self._expand = ctypes.CFUNCTYPE(
            ctypes.c_byte,
            ctypes.c_void_p,
            ctypes.c_void_p,
        )(module._handle + layout.safe_expand_rva)
        self._destructor = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(
            module._handle + layout.safe_destructor_rva
        )

    def _decode_inner(self, inner: bytes) -> bytes:
        obj = ctypes.create_string_buffer(OBJECT_SIZE)
        self._ctor(ctypes.addressof(obj))
        try:
            source = ctypes.create_string_buffer(inner)
            source_base = ctypes.c_uint64(ctypes.addressof(source))
            offset = ctypes.c_uint32(0)
            accepted = self._read(
                ctypes.addressof(obj),
                ctypes.byref(source_base),
                ctypes.addressof(source) + len(inner),
                ctypes.byref(offset),
            )
            if not accepted:
                raise AssertionError(
                    f"{self._layout.label} rejected safe 509: "
                    f"wire_length={len(inner)}, consumed={offset.value}"
                )
            if offset.value != len(inner):
                raise AssertionError(
                    f"{self._layout.label} left a safe 509 suffix: "
                    f"wire_length={len(inner)}, consumed={offset.value}"
                )
            plain = ctypes.create_string_buffer(self._layout.plain_size)
            self._expand(ctypes.addressof(obj), ctypes.addressof(plain))
            return plain.raw
        finally:
            self._destructor(ctypes.addressof(obj))

    def parse_envelope(self, envelope: bytes) -> bytes:
        if len(envelope) < 13:
            raise AssertionError("28346 envelope is truncated")
        if int.from_bytes(envelope[8:10], "big") != 509:
            raise AssertionError("28346 envelope does not contain Msg509")
        inner_length = int.from_bytes(envelope[10:12], "big")
        if inner_length != len(envelope) - 13:
            raise AssertionError(
                f"28346 inner length mismatch: declared={inner_length}, "
                f"actual={len(envelope) - 13}"
            )
        if envelope[-1] != 1:
            raise AssertionError("28346 envelope mode is not one")

        inner = envelope[12 : 12 + inner_length]
        return self.parse_inner(inner)

    def parse_inner(self, inner: bytes) -> bytes:
        plain = self._decode_inner(inner)
        try:
            self._decode_inner(inner[:-1])
        except AssertionError:
            pass
        else:
            raise AssertionError(
                f"{self._layout.label} accepted a one-byte safe truncation"
            )
        return plain


def verify_observed_branches(plain: bytes) -> None:
    assert u32le(plain, 0x0004) == 142
    assert plain[0x0030] == 2
    assert plain[0x0588] == 1
    assert plain[0x0589] == 5
    assert [
        (u16le(plain, 0x058A + index * 4), u16le(plain, 0x058C + index * 4))
        for index in range(5)
    ] == [(2, 10), (11, 26), (27, 33), (62, 71), (75, 81)]
    assert u32le(plain, 0x0736) == 900
    assert plain[0x073A] == 1
    assert plain[0x9069] == 1
    assert plain[0x906A] == 1
    assert plain[0x906B] == 3
    assert plain[0x906C] == 1
    assert u16le(plain, 0x906D) == 1
    assert u32le(plain, 0x906F) == 10001
    assert u32le(plain, 0x9073) == 74362
    assert u32le(plain, 0x9077) == 3755
    assert plain[0x9B0D] == 2

    # CGameBeginPlayerInfo is a fixed 0x23A-byte object in the captured
    # 21487 direct parser.  Exercise every optional nested collection so an
    # accepted root packet cannot hide a wrong element width or field order.
    player = 0x0797
    assert u32le(plain, player + 0x000) == 10001
    assert u32le(plain, player + 0x004) == 423687
    assert plain[player + 0x008] == 2
    assert (u32le(plain, player + 0x009), u32le(plain, player + 0x00D)) == (1, 8)
    assert (u32le(plain, player + 0x011), u32le(plain, player + 0x015)) == (3, 1)
    assert plain[player + 0x03D : player + 0x03F] == bytes((6, 5))
    assert plain[player + 0x03F] == 1
    assert u16le(plain, player + 0x040) == 100
    assert u16le(plain, player + 0x042) == 200
    assert plain[player + 0x044 : player + 0x046] == bytes((3, 1))
    assert plain[player + 0x1C4] == 3
    assert [u32le(plain, player + 0x1C5 + index * 4) for index in range(3)] == [
        7,
        8,
        9,
    ]
    assert u32le(plain, player + 0x215) == 500
    assert plain[player + 0x219] == 2
    assert (u32le(plain, player + 0x21A), u32le(plain, player + 0x21E)) == (1, 10)
    assert (u32le(plain, player + 0x222), u32le(plain, player + 0x226)) == (3, 30)


def verify_custom_twelve(plain: bytes) -> None:
    assert u32le(plain, 0x0004) == 238
    assert plain[0x0030] == 12
    assert u16le(plain, 0x0081) == 3
    assert plain[0x0113] == 12
    assert plain[0x0796] == 12
    assert plain[0x3450] == 3
    assert u16le(plain, 0x8E65) == 42
    assert u16le(plain, 0x8E67) == 27
    assert u16le(plain, 0xA418) == 40
    assert plain[0xA7C3] == 1
    assert u32le(plain, 0xA7C4) == 0x11223344
    assert plain[0xA862] == 3
    assert [u32le(plain, 0xA863 + index * 4) for index in range(3)] == [
        238,
        461,
        149,
    ]


def verify_profile(
    plain: bytes,
    *,
    load_time: int,
    map_id: int,
    feedback: int,
    anti_drift: int,
    base_mode: int,
    sub_mode: int,
    move_frequency: int,
    max_prop_effects: int,
    cross_no_collision: int,
    record_limit: int,
    switch_flag: int,
    border_type: int,
    special_effect_switch: int,
    special_effect_parameter: int,
    base_mode_ex: int = 0,
) -> None:
    assert u32le(plain, 0x0000) == load_time
    assert u32le(plain, 0x0004) == map_id
    assert plain[0x0619] == feedback
    assert plain[0x074B] == anti_drift
    assert u16le(plain, 0x8E65) == base_mode
    assert u16le(plain, 0x8E67) == sub_mode
    assert plain[0x8E74] == base_mode_ex
    assert u16le(plain, 0x915F) == 10
    assert u32le(plain, 0x916D) == move_frequency
    assert plain[0xA05E] == max_prop_effects
    assert plain[0xA05F] == cross_no_collision
    if record_limit:
        assert plain[0xA060] == 3
        assert u32le(plain, 0xA067) == record_limit
    else:
        assert plain[0xA060] == 0
    assert u32le(plain, 0xA3D9) == switch_flag
    assert u16le(plain, 0xA418) == border_type
    assert plain[0xA41A] == 4
    assert [u32le(plain, 0xA41B + index * 4) for index in range(4)] == [
        200,
        1000,
        3000,
        200,
    ]
    assert u32le(plain, 0xA7A8) == special_effect_switch
    assert u32le(plain, 0xA7AC) == special_effect_parameter


def verify_ranked(plain: bytes) -> None:
    verify_profile(
        plain,
        load_time=25000,
        map_id=396,
        feedback=1,
        anti_drift=0,
        base_mode=40,
        sub_mode=1,
        move_frequency=15,
        max_prop_effects=7,
        cross_no_collision=1,
        record_limit=600,
        switch_flag=0x07D88EB3,
        border_type=3,
        special_effect_switch=14,
        special_effect_parameter=15,
    )


def verify_pinnacle(plain: bytes) -> None:
    verify_profile(
        plain,
        load_time=15000,
        map_id=132,
        feedback=0,
        anti_drift=0,
        base_mode=40,
        sub_mode=5,
        move_frequency=15,
        max_prop_effects=7,
        cross_no_collision=1,
        record_limit=600,
        switch_flag=0x01D10EB3,
        border_type=29,
        special_effect_switch=14,
        special_effect_parameter=15,
    )


def verify_border(plain: bytes) -> None:
    verify_profile(
        plain,
        load_time=20000,
        map_id=320,
        feedback=1,
        anti_drift=0,
        base_mode=1,
        sub_mode=1,
        move_frequency=15,
        max_prop_effects=7,
        cross_no_collision=1,
        record_limit=600,
        switch_flag=0x81F88EB3,
        border_type=4,
        special_effect_switch=14,
        special_effect_parameter=15,
        base_mode_ex=101,
    )


def verify_lucky(plain: bytes) -> None:
    verify_profile(
        plain,
        load_time=10000,
        map_id=251,
        feedback=1,
        anti_drift=0,
        base_mode=1,
        sub_mode=1,
        move_frequency=15,
        max_prop_effects=7,
        cross_no_collision=1,
        record_limit=600,
        switch_flag=0x00D13EB3,
        border_type=30,
        special_effect_switch=0,
        special_effect_parameter=15,
    )


def verify_aurora_common(plain: bytes) -> None:
    assert u32le(plain, 0x0004) == 499
    assert u16le(plain, 0x8E65) == 42
    assert u16le(plain, 0x8E67) == 29
    assert u16le(plain, 0xA418) == 43
    assert int.from_bytes(plain[0xA870:0xA878], "little") == 0x1122334455667788
    assert plain[0xA878] == 0


def verify_aurora(plain: bytes) -> None:
    verify_aurora_common(plain)
    assert u32le(plain, 0xA8F9) == 7
    url_length = u16le(plain, 0xA8FD)
    assert plain[0xA8FF : 0xA8FF + url_length] == b"https://127.0.0.1/aurora-v5.zip"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("payload_directory", type=Path)
    parser.add_argument("--layout", choices=PARSER_LAYOUTS, required=True)
    parser.add_argument("--network-dll", type=Path, required=True)
    parser.add_argument(
        "--dependency-directory",
        type=Path,
        action="append",
        default=[],
    )
    parser.add_argument(
        "--official-safe-pair",
        type=Path,
        nargs=2,
        action="append",
        default=[],
        metavar=("SAFE_INNER", "EXPANDED_FIXED"),
    )
    parser.add_argument(
        "--reencoded-fixed",
        type=Path,
        action="append",
        default=[],
        help="legacy expanded fixed object re-encoded by this server",
    )
    parser.add_argument(
        "--skip-generated-safe",
        action="store_true",
        help="validate direct bodies only; safe bodies target another client build",
    )
    arguments = parser.parse_args()
    if not arguments.network_dll.is_file():
        raise FileNotFoundError(arguments.network_dll)

    layout = PARSER_LAYOUTS[arguments.layout]
    actual_sha256 = hashlib.sha256(arguments.network_dll.read_bytes()).hexdigest()
    if actual_sha256 != layout.network_dll_sha256:
        raise AssertionError(
            f"{layout.label} DLL hash changed: "
            f"expected={layout.network_dll_sha256}, actual={actual_sha256}"
        )
    direct = Direct509Parser(
        arguments.network_dll,
        layout,
        arguments.dependency_directory,
    )
    cases = (
        ("observed-branches.bin", verify_observed_branches),
        ("ranked.bin", verify_ranked),
        ("pinnacle.bin", verify_pinnacle),
        ("border.bin", verify_border),
        ("lucky.bin", verify_lucky),
        ("custom-12p-3map.bin", verify_custom_twelve),
        (
            "aurora.bin",
            verify_aurora if layout.has_aurora_tail else verify_aurora_common,
        ),
    )
    for name, verifier in cases:
        payload = (arguments.payload_directory / name).read_bytes()
        plain = direct.parse(payload)
        if verifier is not None:
            verifier(plain)
        print(
            f"{layout.label} accepted {name}: "
            f"{len(payload)}-byte SendToClient body"
        )

    if layout.safe_read_rva is not None and not arguments.skip_generated_safe:
        safe = Safe509Parser(direct._module, layout)
        safe_paths = sorted(arguments.payload_directory.glob("safe-*.28346.bin"))
        if not safe_paths:
            raise AssertionError("no generated safe 509 fixtures were found")
        for safe_path in safe_paths:
            direct_path = safe_path.with_name(
                safe_path.name.replace(".28346.bin", ".direct.bin")
            )
            expected = direct.parse(direct_path.read_bytes())
            actual = safe.parse_envelope(safe_path.read_bytes())
            if actual != expected:
                first_difference = next(
                    index
                    for index, (left, right) in enumerate(zip(actual, expected))
                    if left != right
                )
                raise AssertionError(
                    f"safe/direct fixed object mismatch for {safe_path.name}: "
                    f"offset={first_difference:#x}, "
                    f"safe={actual[first_difference]:#x}, "
                    f"direct={expected[first_difference]:#x}"
                )
            print(
                f"{layout.label} accepted {safe_path.name}: "
                f"fixed object SHA256={hashlib.sha256(actual).hexdigest()}"
            )
        for fixture_index, (
            safe_inner_path,
            expanded_fixed_path,
        ) in enumerate(arguments.official_safe_pair):
            expected = expanded_fixed_path.read_bytes()
            actual = safe.parse_inner(safe_inner_path.read_bytes())
            if actual != expected:
                raise AssertionError(
                    f"official safe fixture mismatch: {safe_inner_path}"
                )
            print(
                f"{layout.label} reproduced official {safe_inner_path.name}: "
                f"fixed object SHA256={hashlib.sha256(actual).hexdigest()}"
            )
            reencoded_path = (
                arguments.payload_directory
                / f"official-reencoded-{fixture_index}.inner.bin"
            )
            reencoded = safe.parse_inner(reencoded_path.read_bytes())
            if reencoded != expected:
                raise AssertionError(
                    f"server re-encode mismatch for {expanded_fixed_path}"
                )
            print(
                f"{layout.label} accepted server re-encode "
                f"{reencoded_path.name}: fixed object "
                f"SHA256={hashlib.sha256(reencoded).hexdigest()}"
            )
        for fixture_index, expanded_fixed_path in enumerate(
            arguments.reencoded_fixed
        ):
            expected = expanded_fixed_path.read_bytes()
            if len(expected) < layout.plain_size:
                expected += bytes(layout.plain_size - len(expected))
            if len(expected) != layout.plain_size:
                raise AssertionError(
                    f"expanded fixed size mismatch for {expanded_fixed_path}: "
                    f"{len(expected)} != {layout.plain_size}"
                )
            reencoded_path = (
                arguments.payload_directory
                / f"official-reencoded-{fixture_index}.inner.bin"
            )
            actual = safe.parse_inner(reencoded_path.read_bytes())
            if actual != expected:
                first_difference = next(
                    index
                    for index, (left, right) in enumerate(zip(actual, expected))
                    if left != right
                )
                raise AssertionError(
                    f"server re-encode mismatch for {expanded_fixed_path}: "
                    f"offset={first_difference:#x}"
                )
            print(
                f"{layout.label} accepted server re-encode "
                f"{reencoded_path.name}: fixed object "
                f"SHA256={hashlib.sha256(actual).hexdigest()}"
            )
    elif (
        not arguments.skip_generated_safe and
        (arguments.official_safe_pair or arguments.reencoded_fixed)
    ):
        raise AssertionError(
            f"{layout.label} cannot validate supplied safe fixtures"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
