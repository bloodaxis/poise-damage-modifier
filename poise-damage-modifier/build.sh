#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
ZIG="${ZIG:-zig}"
mkdir -p build
"$ZIG" c++ -target x86_64-windows-gnu -std=c++17 -O2 -g -shared -DWIN32_LEAN_AND_MEAN -DNOMINMAX poise_damage.cpp -o build/poise-damage-modifier.dll
