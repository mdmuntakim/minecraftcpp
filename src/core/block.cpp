#include "mcc/block.hpp"

namespace mcc {

const BlockDef kBlocks[BLOCK_COUNT] = {
    /* AIR        */ {"air", 0, 0, 0, false, false, false, 0x000000},
    /* GRASS      */ {"grass", TILE_GRASS_TOP, TILE_GRASS_SIDE, TILE_DIRT,
                      true, true, false, 0x6aa84f},
    /* DIRT       */ {"dirt", TILE_DIRT, TILE_DIRT, TILE_DIRT,
                      true, true, false, 0x8b5e3c},
    /* STONE      */ {"stone", TILE_STONE, TILE_STONE, TILE_STONE,
                      true, true, false, 0x808080},
    /* COBBLE     */ {"cobblestone", TILE_COBBLE, TILE_COBBLE, TILE_COBBLE,
                      true, true, false, 0x6e6e6e},
    /* SAND       */ {"sand", TILE_SAND, TILE_SAND, TILE_SAND,
                      true, true, false, 0xd9cc8f},
    /* WATER      */ {"water", TILE_WATER, TILE_WATER, TILE_WATER,
                      false, false, true, 0x3f6fd8},
    /* LOG        */ {"log", TILE_LOG_TOP, TILE_LOG_SIDE, TILE_LOG_TOP,
                      true, true, false, 0x6b4f2a},
    /* LEAVES     */ {"leaves", TILE_LEAVES, TILE_LEAVES, TILE_LEAVES,
                      true, true, false, 0x3f7a2e},
    /* PLANKS     */ {"planks", TILE_PLANKS, TILE_PLANKS, TILE_PLANKS,
                      true, true, false, 0xb08a52},
    /* BRICK      */ {"brick", TILE_BRICK, TILE_BRICK, TILE_BRICK,
                      true, true, false, 0x9d4b3c},
    /* GLASS      */ {"glass", TILE_GLASS, TILE_GLASS, TILE_GLASS,
                      true, false, false, 0xcfe8f0},
    /* SNOW       */ {"snow", TILE_SNOW, TILE_SNOW, TILE_SNOW,
                      true, true, false, 0xf2f6f8},
    /* GRAVEL     */ {"gravel", TILE_GRAVEL, TILE_GRAVEL, TILE_GRAVEL,
                      true, true, false, 0x8a8a8a},
    /* BEDROCK    */ {"bedrock", TILE_BEDROCK, TILE_BEDROCK, TILE_BEDROCK,
                      true, true, false, 0x3a3a3a},
    /* SANDSTONE  */ {"sandstone", TILE_SANDSTONE, TILE_SANDSTONE, TILE_SANDSTONE,
                      true, true, false, 0xcfc08a},
    /* GLOW       */ {"glowstone", TILE_GLOW, TILE_GLOW, TILE_GLOW,
                      true, true, false, 0xf2c14e},
};

}  // namespace mcc
