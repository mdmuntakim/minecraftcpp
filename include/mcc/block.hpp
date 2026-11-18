// Block and tile definitions. All textures are generated procedurally by the
// engine, there is not a single file copied from another game.
#pragma once

#include <cstdint>

namespace mcc {

enum Tile : uint8_t {
    TILE_GRASS_TOP = 0,
    TILE_GRASS_SIDE,
    TILE_DIRT,
    TILE_STONE,
    TILE_COBBLE,
    TILE_SAND,
    TILE_LOG_SIDE,
    TILE_LOG_TOP,
    TILE_LEAVES,
    TILE_WATER,
    TILE_PLANKS,
    TILE_BRICK,
    TILE_GLASS,
    TILE_SNOW,
    TILE_GRAVEL,
    TILE_BEDROCK,
    TILE_SANDSTONE,
    TILE_GLOW,
    TILE_COUNT
};

enum Block : uint8_t {
    BLOCK_AIR = 0,
    BLOCK_GRASS,
    BLOCK_DIRT,
    BLOCK_STONE,
    BLOCK_COBBLE,
    BLOCK_SAND,
    BLOCK_WATER,
    BLOCK_LOG,
    BLOCK_LEAVES,
    BLOCK_PLANKS,
    BLOCK_BRICK,
    BLOCK_GLASS,
    BLOCK_SNOW,
    BLOCK_GRAVEL,
    BLOCK_BEDROCK,
    BLOCK_SANDSTONE,
    BLOCK_GLOW,
    BLOCK_COUNT
};

struct BlockDef {
    const char* name;
    uint8_t top;
    uint8_t side;
    uint8_t bottom;
    bool solid;
    bool opaque;   // hides the face of the neighbour
    bool liquid;
    uint32_t hudColor;
};

extern const BlockDef kBlocks[BLOCK_COUNT];

inline bool isSolid(uint8_t id) { return id != BLOCK_AIR && kBlocks[id].solid; }
inline bool isOpaque(uint8_t id) { return id != BLOCK_AIR && kBlocks[id].opaque; }
inline bool isLiquid(uint8_t id) { return id != BLOCK_AIR && kBlocks[id].liquid; }

}  // namespace mcc
