# Poise Damage Modifier

Multiplies poise damage in Elden Ring (0 to 100, 1 is vanilla). Mod Settings Menu is optional.

## Install

Download the [latest release](https://github.com/bloodaxis/poise-damage-modifier/releases)
and extract the DLL and INI together into an offline DLL mod folder. Set
`Multiplier` in the INI or use Mod Settings Menu. Set `Enabled=0` to disable.

## Build

Install Zig 0.16.0, then run from the repository root:

```sh
ZIG=/path/to/zig bash poise-damage-modifier/build.sh
python3 poise-damage-modifier/verify_build.py
```

On Windows, run `poise-damage-modifier\build.cmd` and then
`py -3 poise-damage-modifier\verify_build.py`. The build produces a Windows x64
DLL; the verifier checks its executable sections against the release build.

## References

- [techiew/EldenRingMods](https://github.com/techiew/EldenRingMods) — DLL initialization patterns.
- [Nordgaren/Erd-Tools-CPP](https://github.com/Nordgaren/Erd-Tools-CPP) — runtime PARAM access.
- [vswarte/fromsoftware-rs](https://github.com/vswarte/fromsoftware-rs) — FromSoftware formats and tooling.
