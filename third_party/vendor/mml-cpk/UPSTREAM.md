# MikuMikuLibrary CPK and sprite bridge

The bridge is built from the MikuMikuLibrary source snapshot at commit
`82380f1e40bbcf07d19c4ac8d0bdb95845be3628` (MIT License, copyright Skyth).
The vendored source is the exact snapshot used for the successful probe; the
bridge-specific C ABI additions are in `AotBridge/Bridge.cs`.

The bridge uses NativeAOT and does not require a player-installed .NET runtime.
Its UTF-8 C ABI exposes read-only CPK enumeration and single-entry reads through
`CpkArchive`, SpriteDatabase name-to-file/index lookup, and SpriteSet texture
decoding with the actual sprite rectangle. The plugin does not implement the
CPK, SpriteDatabase, or SpriteSet binary formats. Sprite texture decoding
currently supports RGBA8, RGB8, DXT1, DXT1a, DXT3, and DXT5; unsupported formats
return an unavailable result.

Build reproducibly from the project root with `scripts/build.ps1`, which prepares
the selected MSVC and Windows SDK environment before invoking
`scripts/build-module-assets.ps1`. The latter restores the pinned project
references from the local NuGet package cache when available, then publishes
the `win-x64` NativeAOT shared library into `build/release/module-assets` and
copies the runtime bridge beside the host DLL at `build/release/MmlCpkBridge.dll`.
The packaged runtime does not use a player-installed .NET runtime or a fixed
development drive path.

`assimp.dll` is emitted by the upstream `AssimpNet` package and the current build
script copies it beside the bridge. It is not used by the CPK or sprite code
paths, but must be treated as a runtime dependency until the isolated
CPK-plus-sprite smoke test without that DLL passes on the final NativeAOT build.
