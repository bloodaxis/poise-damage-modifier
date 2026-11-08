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
    ".text": "db20ed958f7a35a5f3b4bc97a40c2c813ad9c84e9a7a123fa8f9eee344410c8b",
    ".rdata": "71798e6c71a9b75dae68be6fb9f6f8ad6b310666cf79d18a0025e08f24420e83",
    ".data": "35fd3557ecb474291f069e7f4a2d1a2e3a1c35089bca69ac166d6f0e587f6ec1",
    ".pdata": "3e97da3d918d13f1552ca07c6a8242eb9c33ca0c1b0da5d8d0f844b915de2637",
    ".tls": "076a27c79e5ace2a3d47f9dd2e83e4ff6ea8872b3c2218f66c92b89b55f36560",
    ".reloc": "12454207ce923fd395ca8fd6a8d2d025d910f21fd1b621c93cb4b777a2c61339",
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
