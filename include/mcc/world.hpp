// Chunked voxel world. Generation is pure and deterministic: one seed always
// produces the same terrain, which is why the same world can be rebuilt on the
// device and checked in the host tests.
#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "mcc/block.hpp"

namespace mcc {

constexpr int CHUNK_SX = 16;
constexpr int CHUNK_SZ = 16;
constexpr int WORLD_H = 128;
constexpr int SEA_LEVEL = 44;

struct Chunk {
    int cx = 0;
    int cz = 0;
    int maxY = 0;
    uint32_t revision = 1;
    std::vector<uint8_t> blocks;

    Chunk() : blocks(static_cast<size_t>(CHUNK_SX) * CHUNK_SZ * WORLD_H, 0) {}

    static int index(int lx, int y, int lz) {
        return (y * CHUNK_SZ + lz) * CHUNK_SX + lx;
    }
    static bool inside(int lx, int y, int lz) {
        return lx >= 0 && lx < CHUNK_SX && lz >= 0 && lz < CHUNK_SZ &&
               y >= 0 && y < WORLD_H;
    }
    uint8_t get(int lx, int y, int lz) const {
        return inside(lx, y, lz) ? blocks[static_cast<size_t>(index(lx, y, lz))] : 0;
    }
    void set(int lx, int y, int lz, uint8_t id) {
        if (!inside(lx, y, lz)) return;
        blocks[static_cast<size_t>(index(lx, y, lz))] = id;
        if (y > maxY) maxY = y;
    }
};

class World {
public:
    explicit World(uint32_t seed = 1337U) : seed_(seed) {}

    uint32_t seed() const { return seed_; }

    Chunk* findChunk(int cx, int cz) {
        auto it = chunks_.find(key(cx, cz));
        return it == chunks_.end() ? nullptr : it->second.get();
    }
    const Chunk* findChunk(int cx, int cz) const {
        auto it = chunks_.find(key(cx, cz));
        return it == chunks_.end() ? nullptr : it->second.get();
    }
    Chunk& ensureChunk(int cx, int cz) {
        const uint64_t k = key(cx, cz);
        auto it = chunks_.find(k);
        if (it != chunks_.end()) return *it->second;
        auto chunk = std::make_unique<Chunk>();
        chunk->cx = cx;
        chunk->cz = cz;
        generate(*chunk);
        Chunk& ref = *chunk;
        chunks_.emplace(k, std::move(chunk));
        return ref;
    }

    uint8_t getBlock(int x, int y, int z) const {
        if (y < 0) return BLOCK_BEDROCK;  // bedrock under the world
        if (y >= WORLD_H) return 0;
        const int cx = floorDiv(x, CHUNK_SX);
        const int cz = floorDiv(z, CHUNK_SZ);
        const Chunk* c = findChunk(cx, cz);
        if (!c) return 0;
        return c->get(x - cx * CHUNK_SX, y, z - cz * CHUNK_SZ);
    }

    void setBlock(int x, int y, int z, uint8_t id) {
        if (y < 0 || y >= WORLD_H) return;
        const int cx = floorDiv(x, CHUNK_SX);
        const int cz = floorDiv(z, CHUNK_SZ);
        Chunk* c = findChunk(cx, cz);
        if (!c) return;
        const int lx = x - cx * CHUNK_SX;
        const int lz = z - cz * CHUNK_SZ;
        if (c->get(lx, y, lz) == id) return;
        c->set(lx, y, lz, id);
        c->revision++;
        // A block on a chunk border also changes the mesh of the neighbour.
        if (lx == 0) bumpRevision(cx - 1, cz);
        if (lx == CHUNK_SX - 1) bumpRevision(cx + 1, cz);
        if (lz == 0) bumpRevision(cx, cz - 1);
        if (lz == CHUNK_SZ - 1) bumpRevision(cx, cz + 1);
    }

    bool solidAt(int x, int y, int z) const;
    bool liquidAt(int x, int y, int z) const;

    uint32_t revision(int cx, int cz) const {
        const Chunk* c = findChunk(cx, cz);
        return c ? c->revision : 0;
    }

    // Height of the generated surface (ignores later edits and caves).
    int terrainHeight(int x, int z) const;

    size_t chunkCount() const { return chunks_.size(); }
    void clear() { chunks_.clear(); }

    // Drops chunks farther than radius (in chunks) from the given chunk.
    void trim(int centerCx, int centerCz, int radius);

    bool isDesert(int x, int z) const;
    bool isSnow(int x, int z) const;

private:
    static uint64_t key(int cx, int cz) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
               static_cast<uint32_t>(cz);
    }
    static int floorDiv(int a, int b) {
        return a >= 0 ? a / b : -((-a + b - 1) / b);
    }
    void generate(Chunk& chunk);
    void bumpRevision(int cx, int cz) {
        Chunk* c = findChunk(cx, cz);
        if (c) c->revision++;
    }

    uint32_t seed_;
    std::unordered_map<uint64_t, std::unique_ptr<Chunk>> chunks_;
};

}  // namespace mcc
