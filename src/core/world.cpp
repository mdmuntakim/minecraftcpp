#include "mcc/world.hpp"

#include <cmath>

#include "mcc/block.hpp"
#include "mcc/math.hpp"
#include "mcc/noise.hpp"

namespace mcc {
namespace {

inline bool inChunk(int lx, int lz) {
    return lx >= 0 && lx < CHUNK_SX && lz >= 0 && lz < CHUNK_SZ;
}

}  // namespace

int World::terrainHeight(int x, int z) const {
    const float fx = static_cast<float>(x);
    const float fz = static_cast<float>(z);
    const float continent = fbm2(fx * 0.0055f, fz * 0.0055f, 4, seed_ * 3U + 11U);
    const float hills = fbm2(fx * 0.028f, fz * 0.028f, 3, seed_ * 7U + 101U);
    const float ridge = std::fabs(fbm2(fx * 0.013f, fz * 0.013f, 2, seed_ + 404U));
    float h = 45.0f + continent * 25.0f + hills * 7.0f + ridge * 6.0f;
    int ih = static_cast<int>(h);
    if (ih < 4) ih = 4;
    if (ih > WORLD_H - 16) ih = WORLD_H - 16;
    return ih;
}

bool World::isDesert(int x, int z) const {
    return fbm2(x * 0.0022f, z * 0.0022f, 2, seed_ + 909U) > 0.18f;
}

bool World::isSnow(int x, int z) const {
    return fbm2(x * 0.0018f, z * 0.0018f, 2, seed_ + 1717U) < -0.28f;
}

void World::generate(Chunk& chunk) {
    const int baseX = chunk.cx * CHUNK_SX;
    const int baseZ = chunk.cz * CHUNK_SZ;

    for (int lz = 0; lz < CHUNK_SZ; ++lz) {
        for (int lx = 0; lx < CHUNK_SX; ++lx) {
            const int wx = baseX + lx;
            const int wz = baseZ + lz;
            const int h = terrainHeight(wx, wz);
            const bool desert = isDesert(wx, wz);
            const bool snow = !desert && isSnow(wx, wz) && h > SEA_LEVEL + 4;
            const bool beach = h <= SEA_LEVEL + 1;

            uint8_t top = BLOCK_GRASS;
            uint8_t filler = BLOCK_DIRT;
            uint8_t deep = BLOCK_STONE;
            if (desert || beach) {
                top = BLOCK_SAND;
                filler = BLOCK_SAND;
                deep = desert ? BLOCK_SANDSTONE : BLOCK_STONE;
            }
            if (snow) top = BLOCK_SNOW;

            for (int y = 0; y <= h; ++y) {
                uint8_t id;
                if (y == 0) {
                    id = BLOCK_BEDROCK;
                } else if (y < h - 3) {
                    id = deep;
                } else if (y < h) {
                    id = filler;
                } else {
                    id = top;
                }
                chunk.set(lx, y, lz, id);
            }

            // Caves: two octaves of 3D noise carved below the surface.
            const int caveTop = h - 2;
            for (int y = 3; y <= caveTop; ++y) {
                const float n = noise3(wx * 0.055f, y * 0.075f, wz * 0.055f,
                                       seed_ + 77U) *
                                    0.72f +
                                noise3(wx * 0.11f, y * 0.15f, wz * 0.11f,
                                       seed_ + 177U) *
                                    0.28f;
                if (n > 0.50f) chunk.set(lx, y, lz, BLOCK_AIR);
            }

            // Water fills the ocean up to sea level.
            if (h < SEA_LEVEL) {
                for (int y = h + 1; y <= SEA_LEVEL; ++y) {
                    if (chunk.get(lx, y, lz) == BLOCK_AIR) {
                        chunk.set(lx, y, lz, BLOCK_WATER);
                    }
                }
            }
        }
    }

    // A few trees. They are kept away from the chunk border so that no tree is
    // clipped by a chunk that is generated later.
    for (int lz = 3; lz < CHUNK_SZ - 3; ++lz) {
        for (int lx = 3; lx < CHUNK_SX - 3; ++lx) {
            const int wx = baseX + lx;
            const int wz = baseZ + lz;
            const int h = terrainHeight(wx, wz);
            if (h < SEA_LEVEL + 1) continue;
            if (chunk.get(lx, h, lz) != BLOCK_GRASS) continue;
            const uint32_t r = hash2(wx, wz, seed_ ^ 0x51EDU);
            if (r % 173U != 0U) continue;

            const int trunk = 4 + static_cast<int>((r >> 8) % 3U);
            const int topY = h + trunk;
            if (topY + 1 >= WORLD_H) continue;

            for (int y = h + 1; y <= topY; ++y) chunk.set(lx, y, lz, BLOCK_LOG);
            for (int dy = -2; dy <= 1; ++dy) {
                const int radius = (dy <= -1) ? 2 : 1;
                for (int dz = -radius; dz <= radius; ++dz) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (dx == 0 && dz == 0) continue;
                        if (std::abs(dx) == radius && std::abs(dz) == radius &&
                            (radius == 2 || (dy == 1 && (dx != 0 || dz != 0)))) {
                            continue;
                        }
                        const int y = topY + dy;
                        if (y <= h + 1 || y >= WORLD_H) continue;
                        if (chunk.get(lx + dx, y, lz + dz) == BLOCK_AIR) {
                            chunk.set(lx + dx, y, lz + dz, BLOCK_LEAVES);
                        }
                    }
                }
            }
        }
    }
}

bool World::solidAt(int x, int y, int z) const { return isSolid(getBlock(x, y, z)); }

bool World::liquidAt(int x, int y, int z) const {
    return isLiquid(getBlock(x, y, z));
}

void World::trim(int centerCx, int centerCz, int radius) {
    for (auto it = chunks_.begin(); it != chunks_.end();) {
        const int cx = static_cast<int>(static_cast<int32_t>(it->first >> 32));
        const int cz = static_cast<int>(static_cast<int32_t>(it->first & 0xFFFFFFFFU));
        if (std::abs(cx - centerCx) > radius || std::abs(cz - centerCz) > radius) {
            it = chunks_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace mcc
