"""Make a linked PE file reproducible, like the MSVC/lld /Brepro option.

zig's linker writes a fresh timestamp and CodeView GUID into every build, so
two builds of the same source differ by about 20 bytes and file hashes can't
tell whether the code changed. This replaces those fields with values
derived from a hash of the file with the fields zeroed: the COFF header
TimeDateStamp, every debug directory TimeDateStamp and the RSDS GUID/age.
The PE checksum is recomputed only if the linker set one.

Usage: python3 -I pe_repro.py FILE.dll [FILE.exe ...]
Author: Bloodaxis
"""
import hashlib
import struct
import sys
from pathlib import Path


def file_offset(data, sections, rva):
    for virtual, size, raw, raw_size in sections:
        if virtual <= rva < virtual + max(size, raw_size):
            return raw + rva - virtual
    raise ValueError(f"RVA {rva:#x} is outside every section")


def fields(data):
    """Returns (offset, size) of every field that varies between builds."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE file")
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    if struct.unpack_from("<H", data, optional)[0] != 0x20B:
        raise ValueError("not PE32+")
    result = [(pe + 8, 4)]
    directories = optional + 112
    debug_rva, debug_size = struct.unpack_from("<II", data, directories + 6 * 8)
    table = optional + optional_size
    sections = [struct.unpack_from("<IIII", data, table + i * 40 + 8) for i in range(count)]
    sections = [(v, s, r, rs) for s, v, rs, r in sections]
    if debug_rva and debug_size:
        start = file_offset(data, sections, debug_rva)
        for entry in range(start, start + debug_size, 28):
            result.append((entry + 4, 4))
            kind, size, _, raw = struct.unpack_from("<IIII", data, entry + 12)
            if kind == 2 and size >= 24 and data[raw:raw + 4] == b"RSDS":
                result.append((raw + 4, 20))  # GUID + age
    return result, optional + 64


def reproducible(path):
    data = bytearray(Path(path).read_bytes())
    varying, checksum_at = fields(data)
    had_checksum = struct.unpack_from("<I", data, checksum_at)[0] != 0
    for offset, size in varying:
        data[offset:offset + size] = bytes(size)
    struct.pack_into("<I", data, checksum_at, 0)
    digest = hashlib.sha256(data).digest()
    stamp = digest[:4]
    for offset, size in varying:
        data[offset:offset + size] = stamp if size == 4 else digest[4:20] + b"\1\0\0\0"
    if had_checksum:
        total = 0
        for i in range(0, len(data) - len(data) % 2, 2):
            total += data[i] | data[i + 1] << 8
            total = (total & 0xFFFF) + (total >> 16)
        struct.pack_into("<I", data, checksum_at, (total & 0xFFFF) + len(data))
    Path(path).write_bytes(data)


if __name__ == "__main__":
    for name in sys.argv[1:]:
        reproducible(name)
