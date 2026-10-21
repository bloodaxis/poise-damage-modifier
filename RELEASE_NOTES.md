Initial prototype release of Poise Damage Modifier by Bloodaxis.

The ZIP contains `poise-damage-modifier.dll` and its default INI. Place both in an offline DLL mod folder. The multiplier defaults to 1 and can be set from 0 to 100. Mod Settings Menu is optional.

The [repository README](https://github.com/bloodaxis/poise-damage-modifier#readme) contains Windows and Linux build steps and a Python script for comparing executable PE sections with this release.

SHA-256: `e1c402f24303b28cf38581b55177898e01dff46f208d6420cc7874fc1385fb43` (`poise-damage-modifier.zip`).

## v0.2.0

Updated the supported Elden Ring executable profile and SoloParamRepository signature for the current game build. PARAM row records are copied in one bounded read before locating effects 100620 and 100621, reducing per-row process-memory reads. Unsupported executable or PARAM layouts remain fail-closed.

The DLL behavior and INI settings are unchanged. Mod Settings Menu remains optional.

Release asset: `poise-damage-modifier-v0.2.0.zip`.
ZIP SHA-256: `a23990b99688883f0079486cc1f8d17ce402fb9913fdc331c74af867cd48a21a`.
DLL SHA-256: `a9318b81bdabb978c2d0e7cc8dcd429fb1511c33ad63b3182352c917c00620e5`.
