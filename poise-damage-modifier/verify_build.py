#!/usr/bin/env python3
"""Compare a built DLL with the executable sections of the release DLL.

Author: Bloodaxis. Python 3 standard library only. The .buildid section is
excluded because it varies with the build path and debug information.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
import struct
import sys

EXPECTED = {
    ".text": "e747f6e34bfd48b1f4eacce67116c1c1b685b57151f4ea4e17472877e6ec1aef",
    ".rdata": "713c193567d545cd3adb5d8eb4cc3f6916368219dbbbc1a674b5066bd44ed9a1",
    ".data": "dabf9e9b28e437ffa14376d9f1d7646cc8229c854b953ffb7f513f52c2774e9b",
    ".pdata": "4bdebc44a31160cd570a7bb3e67663a5443c15d1158bf8828a92feef19517e06",
    ".tls": "076a27c79e5ace2a3d47f9dd2e83e4ff6ea8872b3c2218f66c92b89b55f36560",
    ".reloc": "6d6bd3efe23102156af0d55f5fb7417cf952b1617383a931d1966fa4481c0198",
}


def section_hashes(path: Path) -> dict[str, str]:
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise ValueError("not a PE file")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("invalid PE signature")
    machine, count = struct.unpack_from("<HH", data, pe + 4)
    if machine != 0x8664:
        raise ValueError("expected a Windows x64 DLL")
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    table = pe + 24 + optional_size
    hashes = {}
    for index in range(count):
        row = table + index * 40
        name = data[row:row + 8].split(b"\0", 1)[0].decode("ascii")
        size, offset = struct.unpack_from("<II", data, row + 16)
        if offset + size > len(data):
            raise ValueError("section extends beyond the file")
        hashes[name] = hashlib.sha256(data[offset:offset + size]).hexdigest()
    return hashes


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent / "build" / "poise-damage-modifier.dll"
    try:
        actual = section_hashes(path)
    except (OSError, ValueError, struct.error) as error:
        print(f"Cannot verify {path}: {error}", file=sys.stderr)
        return 2
    failed = False
    for name, expected in EXPECTED.items():
        ok = actual.get(name) == expected
        print(f"{name}: {'MATCH' if ok else 'DIFFERENT'}")
        failed |= not ok
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
