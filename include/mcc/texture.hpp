// Procedural texture atlas and bitmap font. Nothing is loaded from disk: the
// pixels are computed at start-up, so the APK contains no copied artwork.
#pragma once

#include <cstdint>
#include <vector>

namespace mcc {

constexpr int ATLAS_GRID = 16;
constexpr int TILE_PX = 16;
constexpr int ATLAS_PX = ATLAS_GRID * TILE_PX;

// RGBA8, ATLAS_PX * ATLAS_PX. Tile 0 is the bottom-left tile and rows are
// stored bottom-up, matching OpenGL texture coordinates.
std::vector<uint8_t> buildTerrainAtlas();

constexpr int FONT_COLS = 16;
constexpr int FONT_ROWS = 8;
constexpr int FONT_CELL = 8;
constexpr int FONT_PX_W = FONT_COLS * FONT_CELL;
constexpr int FONT_PX_H = FONT_ROWS * FONT_CELL;

std::vector<uint8_t> buildFontAtlas();

}  // namespace mcc
