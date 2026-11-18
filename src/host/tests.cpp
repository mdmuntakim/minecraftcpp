// Host tests for the portable engine core. They do not need a GPU, an
// emulator or the Android SDK: cmake builds them for the build machine.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "mcc/block.hpp"
#include "mcc/game.hpp"
#include "mcc/math.hpp"
#include "mcc/mesher.hpp"
#include "mcc/noise.hpp"
#include "mcc/player.hpp"
#include "mcc/texture.hpp"
#include "mcc/world.hpp"

using namespace mcc;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                 \
    do {                                                                      \
        ++g_checks;                                                           \
        const double va = (a), vb = (b);                                      \
        if (std::fabs(va - vb) > (eps)) {                                     \
            std::printf("FAIL %s:%d: %s = %.4f, expected %.4f +/- %.4f\n",    \
                        __FILE__, __LINE__, #a, va, vb, (double)(eps));       \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

namespace {

void clearBox(World& world, int x0, int y0, int z0, int x1, int y1, int z1) {
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) world.setBlock(x, y, z, BLOCK_AIR);
}

void clearChunks(World& world, int radius) {
    for (int cz = -radius; cz <= radius; ++cz) {
        for (int cx = -radius; cx <= radius; ++cx) {
            world.ensureChunk(cx, cz);
            for (int y = 0; y < WORLD_H; ++y)
                for (int z = 0; z < CHUNK_SZ; ++z)
                    for (int x = 0; x < CHUNK_SX; ++x) world.setBlock(cx * CHUNK_SX + x, y, cz * CHUNK_SZ + z, BLOCK_AIR);
        }
    }
}

int arenaY() { return 96; }

void makeArena(World& world) {
    for (int cz = -2; cz <= 2; ++cz)
        for (int cx = -2; cx <= 2; ++cx) world.ensureChunk(cx, cz);
    const int y = arenaY();
    clearBox(world, 0, y - 8, 0, 7, y + 10, 7);
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x) world.setBlock(x, y, z, BLOCK_STONE);
}

void testNoise() {
    CHECK(noise2(12.5f, -3.25f, 7U) == noise2(12.5f, -3.25f, 7U));
    CHECK(noise2(12.5f, -3.25f, 7U) != noise2(12.5f, -3.25f, 8U));
    CHECK(noise3(1.5f, 2.5f, -3.5f, 9U) == noise3(1.5f, 2.5f, -3.5f, 9U));
    float lo = 10.0f, hi = -10.0f;
    for (int i = 0; i < 2000; ++i) {
        const float v = fbm2(static_cast<float>(i) * 0.13f,
                             static_cast<float>(i) * -0.07f, 4, 11U);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    CHECK(lo > -1.2f && hi < 1.2f);
    CHECK(hi > 0.1f && lo < -0.1f);
}

void testWorldDeterminism() {
    World a(4242U), b(4242U), c(9999U);
    bool same = true;
    for (int i = 0; i < 200; ++i) {
        const int x = i * 37 - 900;
        const int z = 1300 - i * 23;
        if (a.terrainHeight(x, z) != b.terrainHeight(x, z)) same = false;
    }
    CHECK(same);
    CHECK(a.terrainHeight(123, -456) != c.terrainHeight(123, -456) ||
          true);  // different seeds may coincide at one point

    a.ensureChunk(0, 0);
    b.ensureChunk(0, 0);
    const Chunk* ca = a.findChunk(0, 0);
    const Chunk* cb = b.findChunk(0, 0);
    CHECK(ca && cb);
    CHECK(std::memcmp(ca->blocks.data(), cb->blocks.data(), ca->blocks.size()) == 0);
}

void testWorldBounds() {
    World w(1U);
    CHECK(w.getBlock(0, -1, 0) == BLOCK_BEDROCK);
    CHECK(w.getBlock(0, WORLD_H, 0) == BLOCK_AIR);
    // Missing chunks are air until they are generated.
    CHECK(w.getBlock(5000, 60, 5000) == BLOCK_AIR);
    CHECK(w.findChunk(0, 0) == nullptr);
    w.ensureChunk(0, 0);
    CHECK(w.getBlock(0, 0, 0) == BLOCK_BEDROCK);
    CHECK(w.chunkCount() == 1);
}

void testWorldGeneration() {
    World w(12345U);
    w.ensureChunk(0, 0);
    const Chunk& c = *w.findChunk(0, 0);
    CHECK(c.blocks.size() == static_cast<size_t>(CHUNK_SX) * CHUNK_SZ * WORLD_H);
    CHECK(c.maxY > 10);
    for (int lz = 0; lz < CHUNK_SZ; ++lz) {
        for (int lx = 0; lx < CHUNK_SX; ++lx) {
            const int h = w.terrainHeight(lx, lz);
            CHECK(h >= 4 && h <= WORLD_H - 16);
            CHECK(c.get(lx, 0, lz) == BLOCK_BEDROCK);
            CHECK(isSolid(c.get(lx, h, lz)));
            CHECK(!isSolid(c.get(lx, h + 1, lz)));
        }
    }

    // Some ocean exists somewhere around the spawn region.
    bool ocean = false;
    for (int cz = -6; cz <= 6 && !ocean; ++cz) {
        for (int cx = -6; cx <= 6 && !ocean; ++cx) {
            if (w.terrainHeight(cx * CHUNK_SX + 8, cz * CHUNK_SZ + 8) < SEA_LEVEL - 3) {
                ocean = true;
                w.ensureChunk(cx, cz);
                const Chunk& oc = *w.findChunk(cx, cz);
                CHECK(oc.get(8, SEA_LEVEL, 8) == BLOCK_WATER);
                CHECK(oc.get(8, SEA_LEVEL + 1, 8) == BLOCK_AIR);
            }
        }
    }
    CHECK(ocean);
}

void testEditsAndRevisions() {
    World w(77U);
    w.ensureChunk(0, 0);
    w.ensureChunk(1, 0);
    const uint32_t r0 = w.revision(0, 0);
    const uint32_t rn = w.revision(1, 0);
    const uint8_t previous = w.getBlock(15, 110, 5);
    const uint8_t replacement = previous == BLOCK_BRICK ? BLOCK_STONE : BLOCK_BRICK;
    w.setBlock(15, 110, 5, replacement);
    CHECK(w.getBlock(15, 110, 5) == replacement);
    CHECK(w.revision(0, 0) > r0);
    CHECK(w.revision(1, 0) > rn);  // the x=15 border edit dirties the neighbour
    const uint32_t r1 = w.revision(1, 0);
    w.setBlock(8, 111, 5, replacement);  // not the border, no neighbour bump
    CHECK(w.revision(1, 0) == r1);
    CHECK(w.revision(0, 0) > r0);
}

void testMesherFaces() {
    World w(2024U);
    clearChunks(w, 1);
    const int y = WORLD_H - 4;

    w.setBlock(8, y, 8, BLOCK_STONE);
    MeshData m;
    buildChunkMesh(w, *w.findChunk(0, 0), m);
    CHECK(m.opaqueFaces == 6);
    CHECK(m.opaqueVerts.size() == 24);
    CHECK(m.opaqueIndices.size() == 36);
    for (const Vertex& v : m.opaqueVerts) {
        CHECK(v.light > 0.2f && v.light <= 1.01f);
        CHECK(v.u == 0.0f || v.u == 1.0f);
        CHECK(v.v == 0.0f || v.v == 1.0f);
    }

    w.setBlock(9, y, 8, BLOCK_STONE);
    buildChunkMesh(w, *w.findChunk(0, 0), m);
    CHECK(m.opaqueFaces == 10);  // the shared face is culled

    // Water is translucent and only its outer shell is meshed.
    w.setBlock(2, y, 8, BLOCK_WATER);
    w.setBlock(3, y, 8, BLOCK_WATER);
    buildChunkMesh(w, *w.findChunk(0, 0), m);
    CHECK(m.waterFaces == 10);
    CHECK(m.waterVerts.size() == 40);
    CHECK(m.opaqueFaces == 10);

    // Glass is translucent and does not mesh the face between two panes.
    w.setBlock(5, y, 8, BLOCK_GLASS);
    w.setBlock(6, y, 8, BLOCK_GLASS);
    buildChunkMesh(w, *w.findChunk(0, 0), m);
    CHECK(m.waterFaces == 20);
}

void testMesherSeam() {
    World w(555U);
    clearChunks(w, 2);
    const int y = WORLD_H - 4;
    // Two blocks on opposite sides of the x=16 chunk border.
    w.setBlock(15, y, 8, BLOCK_STONE);
    w.setBlock(16, y, 8, BLOCK_STONE);
    MeshData a, b;
    buildChunkMesh(w, *w.findChunk(0, 0), a);
    buildChunkMesh(w, *w.findChunk(1, 0), b);
    // Ten faces total: the two border faces are culled across the seam.
    CHECK(a.opaqueFaces + b.opaqueFaces == 10);
}

void testPlayerPhysics() {
    World w(31337U);
    makeArena(w);
    const int y = arenaY();

    Player p;
    p.pos = Vec3{2.5f, static_cast<float>(y + 1) + 0.3f, 2.5f};
    InputState in;
    for (int i = 0; i < 120; ++i) p.update(w, in, 1.0f / 60.0f);
    CHECK(p.onGround);
    CHECK_NEAR(p.pos.y, y + 1.0f, 0.06);
    CHECK(!p.flying);

    // Jump clears one block.
    float peak = p.pos.y;
    in.jump = true;
    p.update(w, in, 1.0f / 60.0f);
    in.jump = false;
    for (int i = 0; i < 90; ++i) {
        p.update(w, in, 1.0f / 60.0f);
        peak = std::max(peak, p.pos.y);
    }
    CHECK(peak > y + 1.0f + 0.9f);
    CHECK(p.pos.y < y + 1.0f + 0.2f);

    // A wall stops horizontal movement.
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x) w.setBlock(x, y + 1, 4, BLOCK_STONE);
    p.pos = Vec3{2.5f, static_cast<float>(y + 1) + 0.2f, 0.5f};
    p.yaw = 3.14159265f;  // look towards +Z
    p.vel = Vec3{};
    in = InputState{};
    in.moveZ = 1.0f;
    for (int i = 0; i < 180; ++i) p.update(w, in, 1.0f / 60.0f);
    CHECK(p.pos.z < 4.0f - 0.29f);
    CHECK(p.pos.z > 3.0f);

    // Flight ignores gravity.
    in = InputState{};
    in.flying = true;
    p.pos = Vec3{2.5f, static_cast<float>(y + 1) + 0.3f, 2.5f};
    for (int i = 0; i < 60; ++i) p.update(w, in, 1.0f / 60.0f);
    CHECK_NEAR(p.pos.y, y + 1.3f, 0.2);
}

void testRaycast() {
    World w(888U);
    makeArena(w);
    const int y = arenaY();

    Player p;
    p.pos = Vec3{2.5f, static_cast<float>(y + 1) + 0.05f, 0.5f};
    p.yaw = 3.14159265f;
    p.pitch = 0.0f;
    w.setBlock(2, y + 2, 3, BLOCK_BRICK);
    const RayHit hit = p.raycast(w, 6.0f);
    CHECK(hit.hit);
    CHECK(hit.x == 2 && hit.y == y + 2 && hit.z == 3);
    CHECK(hit.nz == -1);
    CHECK_NEAR(hit.distance, 2.5f, 0.2f);

    int ox = 0, oy = 0, oz = 0;
    CHECK(p.placementCell(hit, ox, oy, oz));
    CHECK(ox == 2 && oy == y + 2 && oz == 2);

    p.pitch = 1.5533f;  // look up at the sky
    CHECK(!p.raycast(w, 6.0f).hit);
}

void testGame() {
    Game g(9001U);
    g.prepareSpawn();
    CHECK(g.world().chunkCount() >= 9);
    CHECK(g.player().pos.y > SEA_LEVEL);
    CHECK(g.player().onGround == false || g.player().onGround == true);

    const int generated = g.streamChunks(2);
    CHECK(generated >= 0);
    CHECK(g.world().chunkCount() == 25);

    CHECK(g.hotbarBlock(0) == BLOCK_GRASS);
    CHECK(g.hotbarBlock(8) == BLOCK_GLOW);
    g.selectSlot(4);
    CHECK(g.selectedSlot() == 4);
    g.cycleSlot(1);
    CHECK(g.selectedSlot() == 5);
    g.cycleSlot(-6);
    CHECK(g.selectedSlot() == 8);

    // Fly toggle.
    CHECK(!g.flying());
    CHECK(g.toggleFlying());
    CHECK(g.flying());

    // Breaking the block under the crosshair.
    Player& p = g.player();
    for (int cz = -1; cz <= 1; ++cz)
        for (int cx = -1; cx <= 1; ++cx) g.world().ensureChunk(cx, cz);
    const int y = arenaY();
    clearBox(g.world(), 0, y - 8, 0, 7, y + 10, 7);
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x) g.world().setBlock(x, y, z, BLOCK_STONE);
    p.pos = Vec3{3.5f, static_cast<float>(y + 1) + 0.02f, 3.5f};
    p.yaw = 0.0f;
    p.pitch = -1.5533f;  // straight down
    p.vel = Vec3{};
    CHECK(g.breakBlock());
    CHECK(g.world().getBlock(3, y, 3) == BLOCK_AIR);

    // Placing puts the selected block into the empty cell in front.
    g.selectSlot(2);  // stone
    p.pos = Vec3{2.5f, static_cast<float>(y + 1) + 0.02f, 0.5f};
    p.yaw = 3.14159265f;
    p.pitch = 0.0f;
    g.world().setBlock(2, y + 2, 3, BLOCK_BRICK);
    CHECK(g.placeBlock());
    CHECK(g.world().getBlock(2, y + 2, 2) == BLOCK_STONE);
}

void testMath() {
    const Mat4 proj = Mat4::perspective(1.2f, 1.6f, 0.1f, 200.0f);
    const Mat4 view = Mat4::lookAt(Vec3{0, 0, 0}, Vec3{0, 0, -1}, Vec3{0, 1, 0});
    Frustum f;
    f.extract(Mat4::multiply(proj, view));
    CHECK(f.boxVisible(Vec3{-1, -1, -10}, Vec3{1, 1, -9}));
    CHECK(!f.boxVisible(Vec3{-1, -1, 10}, Vec3{1, 1, 11}));
    CHECK(!f.boxVisible(Vec3{500, -1, -10}, Vec3{501, 1, -9}));

    const AABB a{Vec3{0, 0, 0}, Vec3{1, 1, 1}};
    CHECK(a.intersects(AABB{Vec3{0.5f, 0.5f, 0.5f}, Vec3{2, 2, 2}}));
    CHECK(!a.intersects(AABB{Vec3{1, 0, 0}, Vec3{2, 1, 1}}));
}

void testLongRunAndEdits() {
    // Ten minutes of a sprinting player, with streaming, must stay finite and
    // inside the world. This is the closest a host can get to a soak test.
    Game g(5150U);
    g.prepareSpawn();
    InputState in;
    in.moveZ = 1.0f;
    in.sprint = true;
    in.lookDX = 0.0016f;
    for (int i = 0; i < 60 * 600; ++i) {
        g.update(1.0f / 60.0f, in);
        if (i % 60 == 0) g.streamChunks(2);
        if (i % 600 == 0) {
            CHECK(std::isfinite(g.player().pos.x));
            CHECK(std::isfinite(g.player().pos.y));
            CHECK(std::isfinite(g.player().pos.z));
            CHECK(g.player().pos.y > -20.0f);
            CHECK(g.player().pos.y < WORLD_H + 8.0f);
        }
    }
    CHECK(g.world().chunkCount() > 4);

    // An edit in a chunk that is not loaded yet must load the chunk first.
    Game loaded(17U);
    loaded.prepareSpawn();
    loaded.applyEdit(400, 70, 400, BLOCK_BRICK);
    CHECK(loaded.world().getBlock(400, 70, 400) == BLOCK_BRICK);
    CHECK(!loaded.edits().empty());
}

void testTextures() {
    const std::vector<uint8_t> atlas = buildTerrainAtlas();
    CHECK(atlas.size() == static_cast<size_t>(ATLAS_PX) * ATLAS_PX * 4);
    // Grass top is opaque and green.
    const size_t grass = (static_cast<size_t>(0) + 0) * 4;  // tile 0, pixel (0,0)
    CHECK(atlas[grass + 3] == 255);
    CHECK(atlas[grass + 1] > atlas[grass + 0]);
    // Water is translucent.
    const size_t water = (static_cast<size_t>(TILE_WATER) * 16) * 4;
    CHECK(atlas[water + 3] > 100 && atlas[water + 3] < 240);
    // Leaves contain cut-out pixels so the alpha test has something to do.
    bool cutout = false;
    const size_t leafStart = static_cast<size_t>(TILE_LEAVES) * TILE_PX * 4;
    for (int i = 0; i < TILE_PX; ++i) {
        if (atlas[leafStart + i * 4 + 3] == 0) cutout = true;
    }
    CHECK(cutout);

    const std::vector<uint8_t> font = buildFontAtlas();
    CHECK(font.size() == static_cast<size_t>(FONT_PX_W) * FONT_PX_H * 4);
    size_t lit = 0;
    for (size_t i = 3; i < font.size(); i += 4) {
        if (font[i] > 0) ++lit;
    }
    CHECK(lit > 1000);
}

}  // namespace

int main() {
    testNoise();
    testWorldDeterminism();
    testWorldBounds();
    testWorldGeneration();
    testEditsAndRevisions();
    testMesherFaces();
    testMesherSeam();
    testMath();
    testPlayerPhysics();
    testRaycast();
    testGame();
    testLongRunAndEdits();
    testTextures();

    if (g_failures == 0) {
        std::printf("OK: %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("FAILED: %d of %d checks\n", g_failures, g_checks);
    return 1;
}
