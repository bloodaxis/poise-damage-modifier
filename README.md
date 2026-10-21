# Poise Damage Modifier

Author: **Bloodaxis**

An Elden Ring DLL mod that sets `saAttackPowerRate` in the loaded `SpEffectParam` rows **100620** and **100621**. These are the two hand-style effects used by this prototype. The `Multiplier` setting ranges from 0 to 100; **1 is neutral**. The DLL applies the INI value at launch and applies a new value when saved through Mod Settings Menu. It edits the game's loaded PARAM in memory, not game files, and installs no hooks or per-hit checks. Mod Settings Menu is optional.

This is a row-wide edit: any actor carrying either effect receives the changed rate. The precise recipients and whether a live edit updates an already-active effect still need in-game validation. Version `v0.2.0` supports Elden Ring executable timestamp `0x6aabf7e7` and image size `0x5dfd600`, and checks the PARAM layout before writing; unsupported versions leave the PARAM untouched.

## Install

Download `poise-damage-modifier-v0.2.0.zip` from [Releases](https://github.com/bloodaxis/poise-damage-modifier/releases). Extract `poise-damage-modifier.dll` and `poise-damage-modifier.ini` together into an offline DLL mod folder. Edit `[PoiseDamage]` in the INI, or use its slider in Mod Settings Menu. `Enabled=0` skips the PARAM write and menu registration. The log beside the DLL is overwritten each launch.

## Build from source

Install [Zig 0.16.0](https://ziglang.org/download/) for your host system. The source tree includes the Mod Settings Menu API header needed to compile; the menu itself is not needed to build or run the DLL. No Windows SDK, Visual Studio, or MinHook installation is required.

On Windows, open Command Prompt at the repository root and run:

```bat
set "ZIG=C:\path\to\zig.exe"
poise-damage-modifier\build.cmd
```

On Linux, run:

```sh
ZIG=/path/to/zig bash poise-damage-modifier/build.sh
```

If Zig is on `PATH`, omit `ZIG`. Both scripts cross-compile a Windows x64 DLL at `poise-damage-modifier/build/poise-damage-modifier.dll` using:

```text
zig c++ -target x86_64-windows-gnu -std=c++17 -O2 -g -shared -DWIN32_LEAN_AND_MEAN -DNOMINMAX poise_damage.cpp -o build/poise-damage-modifier.dll
```

From the repository root, verify the executable PE sections against the release build with `python3 poise-damage-modifier/verify_build.py` on Linux or `py -3 poise-damage-modifier\verify_build.py` on Windows. The verifier uses only Python 3's standard library. Debug path information can change the full DLL hash between build directories; the verifier compares the sections that matter for the compiled program and excludes the path-dependent `.buildid` section.

The `v0.2.0` release updates executable recognition and reduces PARAM row-table reads during lookup. Its release asset is `poise-damage-modifier-v0.2.0.zip`; SHA-256: `a23990b99688883f0079486cc1f8d17ce402fb9913fdc331c74af867cd48a21a`. The DLL inside is SHA-256 `a9318b81bdabb978c2d0e7cc8dcd429fb1511c33ad63b3182352c917c00620e5`.

Initial release asset SHA-256: `e1c402f24303b28cf38581b55177898e01dff46f208d6420cc7874fc1385fb43` (`poise-damage-modifier.zip`). The DLL inside is SHA-256 `cf540f39e52c261584801741daf211a0d9213ccdaf9bdda79edcb49a0d814ff4`.

## References

- [techiew/EldenRingMods](https://github.com/techiew/EldenRingMods): DLL worker initialization patterns.
- [Nordgaren/Erd-Tools-CPP](https://github.com/Nordgaren/Erd-Tools-CPP): runtime PARAM repository layout and editing patterns.
- [vswarte/fromsoftware-rs](https://github.com/vswarte/fromsoftware-rs): FromSoftware PARAM structures and tooling.
