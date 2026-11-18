#include "mcc/game.hpp"

#include <cmath>

#include "mcc/block.hpp"

namespace mcc {

Game::Game(uint32_t seed) : world_(seed) {
    const uint8_t defaults[9] = {BLOCK_GRASS, BLOCK_DIRT,  BLOCK_STONE, BLOCK_COBBLE,
                                 BLOCK_SAND,  BLOCK_LOG,   BLOCK_PLANKS, BLOCK_GLASS,
                                 BLOCK_GLOW};
    for (int i = 0; i < 9; ++i) hotbar_[i] = defaults[i];
}

void Game::reset(uint32_t seed) {
    world_.clear();
    world_ = World(seed);
    flying_ = false;
    selected_ = 0;
    edits_.clear();
    player_ = Player();
    prepareSpawn();
}

void Game::applyEdit(int x, int y, int z, uint8_t id) {
    if (y < 0 || y >= WORLD_H) return;
    // setBlock is a no-op for a chunk that is not loaded; loading a save may
    // contain edits in chunks that the spawn radius does not cover.
    const int cx = (x >= 0 ? x : x - CHUNK_SX + 1) / CHUNK_SX;
    const int cz = (z >= 0 ? z : z - CHUNK_SZ + 1) / CHUNK_SZ;
    world_.ensureChunk(cx, cz);
    world_.setBlock(x, y, z, id);
    edits_.push_back(BlockEdit{x, y, z, id});
}

void Game::prepareSpawn() {
    // Generate a small island of chunks around the origin, then pick a dry
    // grass column as the spawn point.
    for (int cz = -1; cz <= 1; ++cz) {
        for (int cx = -1; cx <= 1; ++cx) {
            world_.ensureChunk(cx, cz);
        }
    }

    int bestX = 0, bestZ = 0;
    bool found = false;
    for (int r = 0; r <= 40 && !found; ++r) {
        for (int dz = -r; dz <= r && !found; ++dz) {
            for (int dx = -r; dx <= r && !found; ++dx) {
                if (std::abs(dx) != r && std::abs(dz) != r) continue;
                const int h = world_.terrainHeight(dx, dz);
                if (h > SEA_LEVEL + 2) {
                    const int cx = dx / CHUNK_SX;
                    const int cz = dz / CHUNK_SZ;
                    world_.ensureChunk(cx, cz);
                    const uint8_t surface =
                        world_.getBlock(dx, h, dz);
                    if (surface == BLOCK_GRASS || surface == BLOCK_SNOW ||
                        surface == BLOCK_SAND) {
                        bestX = dx;
                        bestZ = dz;
                        found = true;
                    }
                }
            }
        }
    }
    if (!found) {
        bestX = 0;
        bestZ = 0;
        world_.ensureChunk(0, 0);
    }
    player_.respawn(world_, bestX, bestZ);
}

void Game::update(float dt, const InputState& input) {
    InputState in = input;
    in.flying = flying_;
    player_.update(world_, in, dt);
}

bool Game::breakBlock() {
    const RayHit hit = player_.raycast(world_, 6.0f);
    if (!hit.hit) return false;
    if (hit.block == BLOCK_BEDROCK) return false;
    applyEdit(hit.x, hit.y, hit.z, BLOCK_AIR);
    return true;
}

bool Game::placeBlock() {
    const RayHit hit = player_.raycast(world_, 6.0f);
    if (!hit.hit) return false;
    int x, y, z;
    if (!player_.placementCell(hit, x, y, z)) return false;
    if (y < 0 || y >= WORLD_H) return false;
    const uint8_t existing = world_.getBlock(x, y, z);
    if (isSolid(existing)) return false;

    // Never place a block inside the player.
    AABB block{Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
               Vec3{static_cast<float>(x + 1), static_cast<float>(y + 1),
                    static_cast<float>(z + 1)}};
    if (block.intersects(player_.box())) return false;

    applyEdit(x, y, z, static_cast<uint8_t>(hotbar_[selected_]));
    return true;
}

void Game::selectSlot(int slot) {
    if (slot >= 0 && slot < 9) selected_ = slot;
}

void Game::cycleSlot(int delta) {
    selected_ = (selected_ + delta + 9) % 9;
}

int Game::streamChunks(int radius) {
    const int pcx = static_cast<int>(std::floor(player_.pos.x)) / CHUNK_SX;
    const int pcz = static_cast<int>(std::floor(player_.pos.z)) / CHUNK_SZ;
    int missing = 0;
    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (world_.findChunk(pcx + dx, pcz + dz)) continue;
            world_.ensureChunk(pcx + dx, pcz + dz);
            ++missing;
        }
    }
    // Drop chunks well outside the view so memory stays bounded.
    world_.trim(pcx, pcz, radius + 2);
    return missing;
}

}  // namespace mcc
