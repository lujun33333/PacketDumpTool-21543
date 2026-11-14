from __future__ import annotations

import ctypes
import os


class Vector(ctypes.Structure):
    _fields_ = [
        ("first", ctypes.c_void_p),
        ("last", ctypes.c_void_p),
        ("end", ctypes.c_void_p),
    ]


class Encoder:
    def __init__(self, dll, ctor, compress, write, dtor, obj_size, plain_size):
        if dll.parent.is_dir():
            os.add_dll_directory(str(dll.parent))
        self._module = ctypes.WinDLL(str(dll))
        base = self._module._handle
        self._ctor = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(base + ctor)
        self._compress = ctypes.CFUNCTYPE(
            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p
        )(base + compress)
        self._write = ctypes.CFUNCTYPE(
            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p
        )(base + write)
        self._dtor = ctypes.CFUNCTYPE(
            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint
        )(base + dtor)
        self._obj = ctypes.create_string_buffer(obj_size)
        self._obj_size = obj_size
        self.plain_size = plain_size
        self._vector = Vector()

    def encode(self, plain: bytes) -> bytes:
        if len(plain) != self.plain_size:
            raise ValueError(f"plain must be {self.plain_size} bytes")
        ctypes.memset(self._obj, 0, self._obj_size)
        self._ctor(ctypes.addressof(self._obj))
        try:
            source = ctypes.create_string_buffer(plain, self.plain_size)
            self._compress(ctypes.addressof(self._obj), ctypes.addressof(source))
            if self._vector.first:
                self._vector.last = self._vector.first
            self._write(ctypes.addressof(self._obj), ctypes.byref(self._vector))
            if not self._vector.first or not self._vector.last:
                return b""
            return ctypes.string_at(
                self._vector.first,
                self._vector.last - self._vector.first,
            )
        finally:
            self._dtor(ctypes.addressof(self._obj), 0)
