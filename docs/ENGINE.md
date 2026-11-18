# Engine notes

## Data flow

```
seed ──► World::ensureChunk ──► Chunk (16×16×128 bytes)
                                      │
                                      ▼
                        buildChunkMesh (face culling + AO)
                                      │
                     ┌────────────────┴────────────────┐
                     ▼                                 ▼
             opaque vertex/index             translucent vertex/index
                     │                                 │
                     ▼                                 ▼
          GL buffer per chunk/pass        GL buffer per chunk/pass
                     │                                 │
                     └──────────────► Renderer::draw ─┘
```

`World` is a map from chunk coordinates to chunks. `getBlock` and `setBlock`
translate world coordinates to chunk coordinates; a `revision` counter per
chunk is bumped on every write, and a write on a chunk border also bumps the
neighbour. The renderer compares the chunk revision plus a hash of the eight
neighbour revisions with the state of its GPU mesh; that is the whole dirty
tracking.

## Meshing

For every solid block and every one of the six faces, the neighbour is sampled
through a nine-chunk cache (`Sampler`) so the inner loop does no hash map
lookup. A face is emitted only when the neighbour does not hide it:

- an opaque neighbour hides any face;
- the same block hides its own face, which is what makes water/water and
  glass/glass invisible while water/glass stay visible;
- leaves are opaque for meshing but have cut-out pixels in the atlas, which
  the fragment shader discards with an alpha test.

Each face writes four vertices and six indices. The vertex carries position,
tile-local UV, atlas tile index and a baked light value. The light value is the
face shade multiplied by an ambient-occlusion factor computed from the three
blocks that touch each corner; that is what gives the edges of caves and
overhangs their depth without any light propagation pass.

Vertices are chunk-local (0..16); the chunk origin is a uniform at draw time,
which keeps the float precision high far from the origin.

## Rendering

One GLES 3.0 program for the terrain, one for all UI. The terrain program
samples a 256×256 atlas with nearest filtering and a small inset that prevents
bleeding between tiles, multiplies by the baked light, and mixes to the fog
colour by distance. The UI program maps a unit quad to four screen-space
corners, so rectangles, circles (distance-to-centre in the fragment shader),
text glyphs, the splash block and the HUD are all the same pipeline.

Draw order per frame: sky gradient and projected sun, opaque chunk pass with
back-face culling and depth writes, sorted far-to-near translucent pass with
depth writes disabled, then the HUD. Chunks outside the frustum or outside the
render distance are skipped.

## Streaming

`World` generation is synchronous and fast (0.37 ms per chunk on the CI
machine). Each frame `Game::streamChunks(radius + 1)` generates the ring the
camera can see plus one extra ring for correct seam culling. The renderer
rebuilds at most 5 ms worth of dirty chunk meshes per frame; during the splash
screen it uses an 18 ms budget and reports progress. Chunks further than
`radius + 2` are released from the GPU and trimmed from memory by the world.

## Android integration

`android_native_app_glue` drives the lifecycle. `APP_CMD_INIT_WINDOW` creates
the EGL display, an ES 3.0 context and the renderer; `APP_CMD_TERM_WINDOW`
saves, releases the GL resources and tears the surface down; a resize only
updates the viewport and the UI layout. The app has no Java code and the
manifest declares `android:hasCode="false"`; `android.app.NativeActivity`
loads `libminecraftcpp.so` and calls `android_main`.

Touch roles are decided on `ACTION_DOWN`: a button hit box, else the left
thumb-stick half, else look drag. Drag deltas accumulate into `InputState` for
one tick; a short, still tap on the world breaks the block under the
crosshair. Keys are decoded for emulators (`WASD`, `JLIK`, `Space`, `F`, `E`,
`Q`, digits).

## Saving

The compact save is a 40-byte header plus a list of edits:

```
"MCCW" | version u32 | seed u32 | px py pz yaw pitch (f32) |
slot u8 | flying u8 | count u32 | count × (x i32, y i32, z i32, id u8)
```

Terrain is not saved; it is regenerated from the seed and the edits are
replayed on top, which keeps saves small and makes them portable between
versions of the generator as long as the generator itself is unchanged.

## Testing strategy

The core has no GPU and no Android dependency, so it is tested on the host:
generation, meshing counts, seam culling, physics, raycasting, game rules,
textures and a ten-minute soak run. The Android layer is verified by compiling
it for two ABIs, inspecting the ELF machine type and checking the signed APK
with `aapt2` and `apksigner`. What a host cannot verify is on-device frame
time and touch behaviour; that needs a physical arm64 device.
