#include "mcc/mesher.hpp"
#include <algorithm>

#include <cmath>

#include "mcc/block.hpp"

namespace mcc {
namespace {

struct FaceDir {
    int nx, ny, nz;
    int ux, uy, uz;  // tangent, u x v == normal
    int vx, vy, vz;
    int bx, by, bz;  // base corner of the quad
    float shade;
    int tileSlot;    // 0 top, 1 side, 2 bottom
};

// The tangent pair of every face is chosen so that cross(u, v) is the outward
// normal; that makes the triangles front facing without a flip.
const FaceDir kFaces[6] = {
    /* +X */ {1, 0, 0, 0, 0, -1, 0, 1, 0, 1, 0, 1, 0.80f, 1},
    /* -X */ {-1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0.80f, 1},
    /* +Y */ {0, 1, 0, 1, 0, 0, 0, 0, -1, 0, 1, 1, 1.00f, 0},
    /* -Y */ {0, -1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0.45f, 2},
    /* +Z */ {0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0.68f, 1},
    /* -Z */ {0, 0, -1, -1, 0, 0, 0, 1, 0, 1, 0, 0, 0.68f, 1},
};

// Reads blocks around one chunk without hashing the chunk map per access.
class Sampler {
public:
    Sampler(const World& world, const Chunk& center) {
        for (int oz = -1; oz <= 1; ++oz) {
            for (int ox = -1; ox <= 1; ++ox) {
                n_[ox + 1][oz + 1] = world.findChunk(center.cx + ox, center.cz + oz);
            }
        }
        n_[1][1] = &center;
    }

    inline uint8_t at(int x, int y, int z) const {
        if (y < 0) return BLOCK_BEDROCK;
        if (y >= WORLD_H) return BLOCK_AIR;
        int ox = 0, oz = 0;
        if (x < 0) { ox = -1; x += CHUNK_SX; }
        else if (x >= CHUNK_SX) { ox = 1; x -= CHUNK_SX; }
        if (z < 0) { oz = -1; z += CHUNK_SZ; }
        else if (z >= CHUNK_SZ) { oz = 1; z -= CHUNK_SZ; }
        const Chunk* c = n_[ox + 1][oz + 1];
        return c ? c->blocks[static_cast<size_t>(Chunk::index(x, y, z))] : static_cast<uint8_t>(BLOCK_AIR);
    }

private:
    const Chunk* n_[3][3]{};
};

inline bool hidesFace(uint8_t neighbour, uint8_t self) {
    if (isOpaque(neighbour)) return true;
    if (neighbour == self) return true;  // water/water and glass/glass
    return false;
}

inline float aoFactor(int side1, int side2, int corner) {
    if (side1 && side2) return 0.45f;
    const int ao = 3 - (side1 + side2 + corner);
    return 0.45f + 0.55f * (static_cast<float>(ao) / 3.0f);
}

void emitFace(const Sampler& sampler, MeshData& out, const FaceDir& f, int lx, int ly,
              int lz, uint8_t tile, float shade, bool water) {
    std::vector<Vertex>& verts = water ? out.waterVerts : out.opaqueVerts;
    std::vector<uint32_t>& indices = water ? out.waterIndices : out.opaqueIndices;

    const int bx = lx + f.bx, by = ly + f.by, bz = lz + f.bz;
    const int ox = lx + f.nx, oy = ly + f.ny, oz = lz + f.nz;

    const uint32_t base = static_cast<uint32_t>(verts.size());
    for (int c = 0; c < 4; ++c) {
        const int su = c & 1;
        const int sv = (c >> 1) & 1;
        const int du = su ? 1 : -1;
        const int dv = sv ? 1 : -1;

        float light = shade;
        if (!water) {
            const int s1 = isOpaque(sampler.at(ox + du * f.ux, oy + du * f.uy,
                                               oz + du * f.uz));
            const int s2 = isOpaque(sampler.at(ox + dv * f.vx, oy + dv * f.vy,
                                               oz + dv * f.vz));
            const int corner = isOpaque(sampler.at(ox + du * f.ux + dv * f.vx,
                                                   oy + du * f.uy + dv * f.vy,
                                                   oz + du * f.uz + dv * f.vz));
            light *= aoFactor(s1, s2, corner);
        } else {
            light = shade * 0.92f;
        }

        Vertex vtx;
        vtx.x = static_cast<float>(bx + su * f.ux + sv * f.vx);
        vtx.y = static_cast<float>(by + su * f.uy + sv * f.vy);
        vtx.z = static_cast<float>(bz + su * f.uz + sv * f.vz);
        vtx.u = static_cast<float>(su);
        vtx.v = static_cast<float>(sv);
        vtx.tile = static_cast<float>(tile);
        vtx.light = light;
        verts.push_back(vtx);
    }
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);

    if (water) {
        ++out.waterFaces;
    } else {
        ++out.opaqueFaces;
    }
}

}  // namespace

void buildChunkMesh(const World& world, const Chunk& chunk, MeshData& out) {
    out.clear();
    const Sampler sampler(world, chunk);

    const int maxY = std::min(chunk.maxY + 1, WORLD_H - 1);
    for (int y = 0; y <= maxY; ++y) {
        for (int z = 0; z < CHUNK_SZ; ++z) {
            for (int x = 0; x < CHUNK_SX; ++x) {
                const uint8_t id = chunk.get(x, y, z);
                if (id == BLOCK_AIR) continue;
                const BlockDef& def = kBlocks[id];
                // Glass goes into the transparent batch as well: it has an
                // alpha texture and must be blended, not alpha tested.
                const bool water = def.liquid || id == BLOCK_GLASS;

                for (int f = 0; f < 6; ++f) {
                    const FaceDir& face = kFaces[f];
                    const uint8_t neighbour =
                        sampler.at(x + face.nx, y + face.ny, z + face.nz);
                    if (hidesFace(neighbour, id)) continue;

                    uint8_t tile = def.side;
                    if (face.tileSlot == 0) tile = def.top;
                    else if (face.tileSlot == 2) tile = def.bottom;

                    emitFace(sampler, out, face, x, y, z, tile, face.shade, water);
                }
            }
        }
    }
}

}  // namespace mcc
