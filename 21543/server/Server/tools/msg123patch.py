from __future__ import annotations

import ctypes
from ctypes import wintypes

from msg123enc import Encoder


class PinnedEncoder(Encoder):
    def __init__(
        self,
        dll,
        ctor,
        compress,
        write,
        dtor,
        obj_size,
        plain_size,
        rand_iat,
    ):
        super().__init__(dll, ctor, compress, write, dtor, obj_size, plain_size)
        self.rand_value = 0
        self._rand_cb = ctypes.CFUNCTYPE(ctypes.c_int)(lambda: self.rand_value)
        callback = ctypes.cast(self._rand_cb, ctypes.c_void_p).value
        slot = self._module._handle + rand_iat
        old_protect = wintypes.DWORD()
        if not ctypes.windll.kernel32.VirtualProtect(
            ctypes.c_void_p(slot),
            ctypes.sizeof(ctypes.c_void_p),
            0x40,
            ctypes.byref(old_protect),
        ):
            raise ctypes.WinError()
        ctypes.c_void_p.from_address(slot).value = callback
        ignored = wintypes.DWORD()
        ctypes.windll.kernel32.VirtualProtect(
            ctypes.c_void_p(slot),
            ctypes.sizeof(ctypes.c_void_p),
            old_protect,
            ctypes.byref(ignored),
        )
