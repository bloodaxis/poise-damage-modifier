# Poise Damage Modifier

Author: **Bloodaxis**

Sets `saAttackPowerRate` in loaded `SpEffectParam` rows 100620 and 100621. The
multiplier ranges from 0 to 100; 1 is neutral. Mod Settings Menu is optional.
This row-wide edit affects any actor carrying either effect; in-game recipients
and updates to already-active effects have not been fully verified.

Version `v0.2.0` supports Elden Ring executable timestamp `0x6aabf7e7` and image
size `0x5dfd600`. Unsupported executable or PARAM layouts are left untouched.

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
