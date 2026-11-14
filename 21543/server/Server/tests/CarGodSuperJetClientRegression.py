"""Decode real 509 fixtures and execute the current client's super-jet flag gate.

Read-only against client DLLs; this is not an in-game UI/effect test.
"""
from pathlib import Path
import argparse
import hashlib
import sys

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64
from unicorn.x86_const import UC_X86_REG_RDX, UC_X86_REG_RIP

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import extract_current_509_codec as codec


def decode(dll, envelope_path):
    b = envelope_path.read_bytes()
    inner = b[12:12 + int.from_bytes(b[10:12], 'big')]
    uc = codec.new_unicorn(dll)
    uc.mem_write(codec.OBJ, b'\0' * codec.OBJ_SIZE)
    codec.call(uc, codec.BASE + codec.CTOR_RVA, codec.OBJ)
    uc.mem_write(codec.INP, inner)
    uc.mem_write(codec.CUR, codec.INP.to_bytes(8, 'little'))
    uc.mem_write(codec.CUR + 8, b'\0' * 4)
    result = codec.call(uc, codec.BASE + codec.DECODER_RVA, codec.OBJ,
                        codec.CUR, codec.INP + len(inner), codec.CUR + 8)
    consumed = int.from_bytes(uc.mem_read(codec.CUR + 8, 4), 'little')
    assert result == 1 and consumed == len(inner), (result, consumed, len(inner))
    uc.mem_write(codec.OUT, b'\0' * codec.OUT_SIZE)
    codec.call(uc, codec.BASE + codec.EXPORTER_RVA, codec.OBJ, codec.OUT)
    return bytes(uc.mem_read(codec.OUT, codec.FIXED_RAW_SIZE))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--network', type=Path, required=True)
    ap.add_argument('--topkart', type=Path, required=True)
    ap.add_argument('--payloads', type=Path, required=True)
    args = ap.parse_args()
    pe = pefile.PE(str(args.topkart), fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    gate = base + 0x8B39A4
    # test byte ptr [rdx+0xA3D9],0x40; jne disabled-return-zero
    expected_code = bytes.fromhex('f6 82 d9 a3 00 00 40 0f 85 e5 01 00 00')
    assert pe.get_data(0x8B39A4, len(expected_code)) == expected_code, 'client gate changed'
    states = []
    for name, expected_flag, expected_ip in (
        ('safe-cargod-skin-163044', 0x009133F0, base + 0x8B3B96),
        ('safe-cargod-superjet-enabled-skin-163044', 0x009133B0, base + 0x8B39B1),
    ):
        raw = decode(args.network, args.payloads / (name + '.28346.bin'))
        flag = int.from_bytes(raw[0xA3D9:0xA3DD], 'little')
        kart = int.from_bytes(raw[0x118:0x11C], 'little')
        assert flag == expected_flag and kart == 163044, (name, flag, kart)
        uc = Uc(UC_ARCH_X86, UC_MODE_64)
        uc.mem_map(base + 0x8B3000, 0x1000)
        uc.mem_write(base + 0x8B3000, pe.get_data(0x8B3000, 0x1000))
        data = 0x500000
        uc.mem_map(data, 0x10000)
        uc.mem_write(data + 0xA3D9, flag.to_bytes(4, 'little'))
        uc.reg_write(UC_X86_REG_RDX, data)
        uc.emu_start(gate, base + 0x8B4000, count=2)
        ip = uc.reg_read(UC_X86_REG_RIP)
        assert ip == expected_ip, (name, hex(ip), hex(expected_ip))
        states.append(raw)
        print(f'PASS {name}: SyncCarID={kart} SwitchFlag={flag:#010x} '
              f'Disabled={int(bool(flag & 0x40))} ClientGateNext={ip:#x}', flush=True)
    changes = [i for i, (a, b) in enumerate(zip(*states)) if a != b]
    assert changes == [0xA3D9], changes
    assert states[0][0xA3D9] ^ states[1][0xA3D9] == 0x40
    for p in (args.network, args.topkart):
        print(f'SHA256 {p.name} {hashlib.sha256(p.read_bytes()).hexdigest()}')
    print('PASS: only DisableKartSuperJet changed; current client no longer takes its zero-feature branch.')


if __name__ == '__main__':
    main()
