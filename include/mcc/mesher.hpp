// Face-culled chunk mesher with per-vertex ambient occlusion. Vertices are in
// chunk local coordinates (0..16) so the GPU never sees a large world
// coordinate; the chunk origin is a uniform translate.
#pragma once

#include <cstdint>
#include <vector>

#include "mcc/world.hpp"

namespace mcc {

struct Vertex {
    float x, y, z;
    float u, v;
    float tile;
    float light;
};

struct MeshData {
    std::vector<Vertex> opaqueVerts;
    std::vector<uint32_t> opaqueIndices;
    std::vector<Vertex> waterVerts;
    std::vector<uint32_t> waterIndices;
    size_t opaqueFaces = 0;
    size_t waterFaces = 0;

    void clear() {
        opaqueVerts.clear();
        opaqueIndices.clear();
        waterVerts.clear();
        waterIndices.clear();
        opaqueFaces = waterFaces = 0;
    }
    bool empty() const { return opaqueIndices.empty() && waterIndices.empty(); }
};

void buildChunkMesh(const World& world, const Chunk& chunk, MeshData& out);

}  // namespace mcc
