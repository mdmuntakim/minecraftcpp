// Headless benchmark of the parts of the engine that do not need a GPU:
// terrain generation, chunk meshing, the tick and the texture bake. The
// numbers are printed so a release note can quote them.
#include <chrono>
#include <cstdio>
#include <vector>

#include "mcc/game.hpp"
#include "mcc/mesher.hpp"
#include "mcc/player.hpp"
#include "mcc/texture.hpp"
#include "mcc/world.hpp"

using namespace mcc;
using Clock = std::chrono::steady_clock;

static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int main() {
    constexpr int kChunks = 121;  // 11 x 11, the default view distance
    constexpr int kRadius = 5;

    std::printf("MinecraftC++ core benchmark\n");
    std::printf("compiler: %s\n",
#if defined(__clang__)
                "clang"
#elif defined(__GNUC__)
                "gcc"
#else
                "unknown"
#endif
    );

    // Terrain generation.
    auto t0 = Clock::now();
    World world(1234567U);
    for (int cz = -kRadius; cz <= kRadius; ++cz)
        for (int cx = -kRadius; cx <= kRadius; ++cx) world.ensureChunk(cx, cz);
    auto t1 = Clock::now();
    const double genMs = ms(t0, t1);
    std::printf("terrain:  %d chunks in %8.2f ms  (%6.2f ms/chunk)\n", kChunks,
                genMs, genMs / kChunks);

    // Meshing.
    MeshData mesh;
    size_t faces = 0, verts = 0;
    t0 = Clock::now();
    for (int cz = -kRadius; cz <= kRadius; ++cz) {
        for (int cx = -kRadius; cx <= kRadius; ++cx) {
            const Chunk* c = world.findChunk(cx, cz);
            buildChunkMesh(world, *c, mesh);
            faces += mesh.opaqueFaces + mesh.waterFaces;
            verts += mesh.opaqueVerts.size() + mesh.waterVerts.size();
        }
    }
    t1 = Clock::now();
    const double meshMs = ms(t0, t1);
    std::printf("meshing:  %d chunks in %8.2f ms  (%6.2f ms/chunk)\n", kChunks,
                meshMs, meshMs / kChunks);
    std::printf("geometry: %zu faces, %zu vertices, %.1f KiB of vertex data\n", faces,
                verts, static_cast<double>(verts * sizeof(Vertex)) / 1024.0);

    // Tick: one second of simulation at 60 Hz with the player walking.
    Game game(1234567U);
    game.prepareSpawn();
    game.streamChunks(kRadius);
    InputState in;
    in.moveZ = 1.0f;
    in.sprint = true;
    t0 = Clock::now();
    constexpr int kTicks = 60 * 600;  // ten minutes of play
    for (int i = 0; i < kTicks; ++i) game.update(1.0f / 60.0f, in);
    t1 = Clock::now();
    const double tickMs = ms(t0, t1);
    std::printf("tick:     %d ticks in %8.2f ms  (%6.4f ms/tick)\n", kTicks, tickMs,
                tickMs / kTicks);

    // Texture bake.
    t0 = Clock::now();
    const std::vector<uint8_t> atlas = buildTerrainAtlas();
    const std::vector<uint8_t> font = buildFontAtlas();
    t1 = Clock::now();
    std::printf("textures: %zu KiB terrain + %zu KiB font in %.2f ms\n",
                atlas.size() / 1024, font.size() / 1024, ms(t0, t1));

    std::printf("\nbudget: %.2f ms of CPU work per new chunk (generate + mesh), "
                "%.3f ms per tick\n",
                genMs / kChunks + meshMs / kChunks, tickMs / kTicks);
    return 0;
}
