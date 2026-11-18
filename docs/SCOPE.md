# Scope: what "the whole Minecraft in C++" can and cannot be

The issue asked for the complete game to be downloaded, its Java logic
translated to C++ and an APK that contains "the entire latest Minecraft",
with an official icon, the official splash screen and a promise of five times
the performance of Bedrock Edition.

This document explains what was built instead and why, so the result is not
mistaken for something it is not.

## What cannot be done in this repository

1. **Minecraft's source is not public.** The Java edition is shipped as
   obfuscated jars, the Bedrock edition as native binaries. Decompiling them
   and republishing the result is a copyright violation, and this project
   cannot ship that code or those assets.
2. **The assets are copyrighted too.** Textures, sounds, the font, the logo,
   the splash screen and the official icon belong to Mojang. Copying them into
   an APK is not permitted, whether the APK is free or not.
3. **"The entire latest Minecraft" is not a build artefact.** It is a
   commercial product with a decade of content, networking, redstone, mobs,
   world formats, Java/Bedrock protocol compatibility and a store ecosystem.
   Even a clean-room reimplementation of the survival game loop is a
   multi-year project, not something a single unattended run can produce.
4. **"5× faster than Bedrock" cannot be verified here.** Bedrock is a highly
   optimised C++ engine, and this environment has no Android device or
   emulator to run either engine on. Any such number would be invented. The
   release notes therefore contain measured CPU numbers for this engine only.

## What was built

A real, installable, playable foundation that matches the parts of the request
that can be done legally and honestly:

- the app name **MinecraftC++**, package **`tw.jjala.minecraftc`**;
- a landscape, fullscreen, touch-first first-person block game;
- generated icon in the requested spirit (voxel block, 3D "C++", blurred
  background) drawn from scratch;
- a launch screen and an in-engine splash with a loading bar;
- pocket-edition-style controls (thumb stick left, look right, tap to mine,
  MINE/BUILD/JUMP/FLY buttons, hotbar);
- an original texture set: grass, dirt, stone, cobble, sand, log, leaves,
  water, planks, brick, glass, snow, gravel, bedrock, sandstone, glowstone;
- a persistent world: seed, player state and edits survive a restart.

## Parity table

| Minecraft feature | This build | Notes |
| --- | --- | --- |
| Infinite terrain | yes | 16×16×128 chunks, streaming radius 5 |
| Caves, trees, oceans, deserts, snow | yes | procedural, deterministic |
| Mining and building | yes | 9-block hotbar, DDA raycast |
| Water and swimming | yes | translucent pass, buoyancy |
| Creative flight | yes | double-tap JUMP or FLY |
| Pocket-edition controls | yes | thumb stick, buttons, look drag |
| Lighting | baked | per-vertex AO and face shading, no dynamic light |
| Day/night cycle | no | fixed morning sky |
| Survival, mobs, health, hunger | no | the player is always in creative mode |
| Crafting, inventory beyond the hotbar | no | |
| Redstone, villagers, nether, end | no | |
| Multiplayer | no | |
| Java world/save compatibility | no | own compact save format |
| Bedrock parity | no | different engine and content |

The next honest steps, in order of value, would be: greedy meshing and
background worker threads for chunk builds, a day/night cycle with a sun and
moon, survival mechanics and crafting, then mobs and world persistence in a
region format.
