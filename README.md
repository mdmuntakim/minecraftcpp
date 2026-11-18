# MinecraftC++

A clean-room **C++ voxel sandbox for Android arm64**. The app is called
MinecraftC++, the package is `tw.jjala.minecraftc`, and it is one native
library behind a `NativeActivity`: no Java, no Kotlin, no game engine, no
WebView.

It is **not** a copy, port or decompilation of Minecraft. It is an original C++
engine written for this project that plays in the same genre: a first-person
block world, caves, trees, water, mining and building, with touch controls in
the style of the pocket edition. Every texture, the icon and the launch screen
are generated procedurally at build time, so the repository and the APK contain
no Mojang code or art. See [docs/SCOPE.md](docs/SCOPE.md) for the honest scope.

## What is implemented

- **Chunked world** – 16×16×128 chunks, deterministic seeded terrain with
  continents, hills, beaches, deserts, snow, caves, trees, oceans and bedrock.
- **Infinite streaming** – chunks around the player are generated and meshed
  as they come into view and released when they leave it.
- **Face-culled meshing** with per-vertex ambient occlusion, baked face shading
  and a separate translucent pass for water and glass. Chunk borders are
  stitched, so no seam faces are drawn.
- **Player physics** – AABB collision, walking, sprinting, jumping, swimming,
  creative flight, fall-out protection.
- **Block interaction** – DDA raycast, break blocks, place any hotbar block,
  bedrock is indestructible, blocks cannot be placed inside the player.
- **Handheld controls** – fixed left thumb stick, right-side look drag, tap to
  mine, MINE / BUILD / JUMP / FLY / fly up / fly down buttons, nine-slot
  hotbar, double-tap JUMP to toggle flight, keyboard and mouse support for
  emulators.
- **HUD** – crosshair, hotbar previews, selected block name, FPS, position,
  chunk and face counters, underwater tint, toasts.
- **Splash screen** – native launch screen and an in-engine loading screen with
  progress while the spawn chunks are built.
- **Saving** – the seed, player state, hotbar, flight mode and every block edit
  are written to internal storage on pause and every 30 s.
- **No assets on disk** – the 16×16 terrain atlas and the 5×7 bitmap font are
  computed in C++ at start-up.

## Controls

| Action | Touch | Keyboard |
| --- | --- | --- |
| Move | left thumb stick | `W` `A` `S` `D` / arrows |
| Look | drag on the right side | `J` `L` `I` `K` |
| Jump / fly up | JUMP | `Space` |
| Fly down | FLY DOWN (while flying) | `Shift` |
| Toggle flight | FLY, or double-tap JUMP | `F` |
| Mine | MINE, or tap the world | `Q` |
| Place | BUILD | `E` |
| Hotbar | tap a slot | `1` … `9` |

## Build

Requirements: CMake ≥ 3.20, Ninja, a C++17 compiler, JDK for `keytool` /
`apksigner`, the Android SDK with build-tools and **NDK r27 or newer**, and
`zlib` headers on the build machine (only for the icon generator; without them
the PNGs are written uncompressed).

```bash
export ANDROID_HOME=$HOME/Android/Sdk
./build.sh          # host tests + benchmark + signed APK
./build.sh host     # host tests + benchmark only
./build.sh apk      # the APK
./build.sh check    # host tests + native build for every supported ABI
```

The APK is written to `dist/MinecraftCpp-0.1.0-arm64-v8a.apk`. It is signed
with a project key generated in `build/keys/` on first use; keep that file if
you want upgrades to install over an existing copy.

### ABI and device support

| ABI | Role | Status |
| --- | --- | --- |
| `arm64-v8a` | shipped in the APK | compiled, ELF verified as AArch64 |
| `x86_64` | compile-only portability check | compiled, ELF verified as x86-64 |

Minimum Android 5.0 (API 21), target API 35, landscape, OpenGL ES 3.0
required. The APK contains only the arm64 library.

## Tests

The engine core is portable, so the tests run on the build machine:

```bash
./build.sh host
```

- `mcc_tests` – 1,477 assertions over noise determinism, world generation,
  water, edits and chunk revisions, face culling across chunk seams, ambient
  occlusion ranges, collision, jumping, walls, flight, raycasting, placement,
  hotbar logic, frustum culling, both procedural textures and a ten-minute
  sprint/streaming soak test.
- `mcc_bench` – headless benchmark of generation, meshing, the tick and the
  texture bake.
- `./build.sh check` – additionally compiles the renderer and the native entry
  point for arm64-v8a and x86_64 and checks the ELF machine type.

## Measured performance

On the GitHub Actions Linux runner (single core, Release, gcc):

| Stage | Result |
| --- | --- |
| Terrain generation | 0.37 ms per chunk (121 chunks in 44 ms) |
| Meshing with AO | 0.26 ms per chunk (121 chunks in 31 ms) |
| Simulation tick | 0.0001 ms per tick |
| Spawn area (121 chunks) | ~75 ms of CPU work, spread over the splash screen |
| Geometry at view distance 5 | 84,541 faces, 9.0 MiB of vertex data |

The renderer is built around one draw call per chunk and per pass, frustum
culling per chunk, a baked light term per vertex, near/far sorted transparent
chunks and a single texture atlas. The numbers above are CPU only; no GPU
timing was taken because this environment has no Android device or emulator.

## Layout

```
include/mcc/        portable core headers (math, noise, blocks, world, mesher, player, game)
src/core/           portable core implementation
src/android/        NativeActivity entry point, EGL/GLES3 renderer, HUD, input, saving
src/host/           host unit tests and benchmark
tools/icongen.cpp   generates the launcher icon and splash screen at build time
res/                theme only (the icon and splash are generated)
AndroidManifest.xml NativeActivity, package tw.jjala.minecraftc, arm64
build.sh            host + Android build, packaging, signing, verification
docs/               engine notes and the scope statement
```

## Trademarks and assets

Minecraft is a trademark of Mojang Synergies AB. This project is not
affiliated with, endorsed by or sponsored by Mojang or Microsoft. It ships no
Mojang code, textures, sounds, fonts or icons. The launcher icon is an original
voxel illustration with 3D "C++" lettering, not the official Minecraft icon,
and the splash screen is drawn by `tools/icongen.cpp`.

The source of this project is MIT licensed; the generated art is part of the
project.
