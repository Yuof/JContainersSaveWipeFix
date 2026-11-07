# JContainers Save Wipe Fix

SKSE plugin for JContainers. Fixes a race that makes a save lose all its JContainers data, for every mod using
JContainers: the save log (`JContainers64.log`) says `'Save' throws ...` and loading that save reports
`0 objects total`. If the deleted object's memory hasn't been reused yet, the same race crashes the game while saving
instead (on 4.3.3: `JContainers64.dll+1911D8`, `mov rax,[r9]`).

JContainers' autorelease queue deletes a temporary object once it has no owners, but it checks that without a lock. A
script that stores the object into a map in the moment between the check and the delete leaves the map pointing at a
deleted object, and the next save walks that map. The window is a few instructions wide, so it is rare; when it hits,
nothing shows until a later load.

The plugin repeats the check inside the delete, while it holds the registry's write lock, when a script can no longer
fetch the object. If the object has an owner by then it stays, and is queued again when that owner lets it go. Nothing
else changes, and no game code is touched.

## Supported JContainers builds
Every build on the JContainers Nexus page from 4.1.12 on (the code in question is unchanged since then):

| JContainers | Game |
|---|---|
| 4.3.4, 4.1.13, 4.1.12 | SE 1.5.97 |
| 4.3.3, 4.3.0, 4.2.13.1, 4.2.9 | AE 1.6.1170 |
| 4.3.2 | AE 1.7.104 |
| 4.2.8, 4.2.7 | AE 1.6.1130 |
| 4.2.3 | AE 1.6.640 |
| 4.2.2, 4.2.1, 4.2.0 | AE 1.6.353, 1.6.342, 1.6.323 |
| GOG 4.3.2, GOG 4.2.12, GOG 4.2.6 | GOG 1.6.1179, 1.6.659 |
| VR 4.3.2, VR 4.2.11, VR 4.1.13 | VR 1.4.15 |

Each build is recognised by its DLL name, PE timestamp and a hash of the patched function. On any other build, or if
another plugin changed that code, the plugin patches nothing and says so in `JContainersSaveWipeFix.log`.

## Requirements
- [SKSE64](https://skse.silverlock.org/) (or SKSE VR / SKSE GOG)
- [JContainers SE](https://www.nexusmods.com/skyrimspecialedition/mods/16495), one of the builds above

## Building
Visual Studio 2022 or newer (C++; the preset uses the newest one installed), CMake 3.24+,
[vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set.

```
git clone --recurse-submodules https://github.com/Yuof/JContainersSaveWipeFix
cd JContainersSaveWipeFix
cmake --preset release
cmake --build --preset release
```

The DLL is `build/Release/JContainersSaveWipeFix.dll`.

To reproduce the race, build with the environment variable `CL=/DJC_WIDEN_MS=500 /DJC_WIDEN_ONLY` (a sleep inside the
window, without the fix; drop `/DJC_WIDEN_ONLY` to test the fix under the same sleep).

## License
[GPL-3.0-or-later](LICENSE), as required by [CommonLibSSE NG](https://github.com/alandtse/CommonLibVR) (GPL-3.0-or-later
with its modding exceptions), which this plugin links statically.
