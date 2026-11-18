#include "mcc/texture.hpp"

#include <cmath>

#include "mcc/block.hpp"
#include "mcc/math.hpp"
#include "mcc/noise.hpp"

namespace mcc {
namespace {

struct Canvas {
    uint8_t* data;
    int ox, oy;  // tile origin in pixels, y counted from the bottom

    void set(int x, int y, Color c) {
        if (x < 0 || x >= TILE_PX || y < 0 || y >= TILE_PX) return;
        uint8_t* p = data + (static_cast<size_t>(oy + y) * ATLAS_PX + (ox + x)) * 4;
        p[0] = static_cast<uint8_t>(clampf(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
        p[1] = static_cast<uint8_t>(clampf(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
        p[2] = static_cast<uint8_t>(clampf(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
        p[3] = static_cast<uint8_t>(clampf(c.a, 0.0f, 1.0f) * 255.0f + 0.5f);
    }

    void fill(Color c) {
        for (int y = 0; y < TILE_PX; ++y)
            for (int x = 0; x < TILE_PX; ++x) set(x, y, c);
    }
};

inline Color hex(uint32_t v, float a = 1.0f) { return Color::rgb(v, a); }

// Small deterministic per-pixel jitter in [lo, hi].
inline float jitter(int x, int y, int salt, float lo, float hi) {
    const uint32_t h = hash2(x + salt * 31, y - salt * 17, 0x9E3779B9U);
    return lo + (hi - lo) * unitFloat(h);
}



void paintGrassTop(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            const float n = jitter(x, y, 1, -0.10f, 0.10f);
            const bool speck = hash2(x, y, 4) % 23 == 0;
            Color col = hex(0x6FA845).scaled(1.0f + n);
            if (speck) col = col.scaled(0.84f);
            c.set(x, y, col);
        }
    }
}

void paintGrassSide(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            const int edge = 3 + static_cast<int>(hash2(x, 0, 9) % 3);
            Color col = hex(0x8B5E3C).scaled(1.0f + jitter(x, y, 3, -0.12f, 0.12f));
            if (hash2(x, y + 7, 12) % 19 == 0) col = col.scaled(0.85f);
            if (y >= TILE_PX - edge) {
                col = hex(0x6FA845).scaled(1.0f + jitter(x, y, 5, -0.12f, 0.08f));
            }
            c.set(x, y, col);
        }
    }
}

void paintDirt(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            Color col = hex(0x8B5E3C).scaled(1.0f + jitter(x, y, 7, -0.14f, 0.14f));
            if (hash2(x, y, 21) % 31 == 0) col = col.scaled(0.78f);
            c.set(x, y, col);
        }
    }
}

void paintStone(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            Color col = hex(0x8A8A8A).scaled(1.0f + jitter(x, y, 11, -0.10f, 0.10f));
            if (hash2(x / 2, y / 2, 33) % 17 == 0) col = col.scaled(0.9f);
            c.set(x, y, col);
        }
    }
}

void paintCobble(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            const int cellX = x / 5;
            const int cellY = y / 4;
            const int shift = static_cast<int>(hash2(cellX, cellY, 44) % 5);
            const bool mortar = ((x + shift) % 5 == 0) || ((y + cellX) % 4 == 0);
            Color col = hex(0x8E8E8E).scaled(
                1.0f + jitter(x, y, 13, -0.13f, 0.13f) + static_cast<float>(cellX % 3) * 0.02f);
            if (mortar) col = hex(0x5F5F5F);
            c.set(x, y, col);
        }
    }
}

void paintSand(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            Color col = hex(0xE0D49A).scaled(1.0f + jitter(x, y, 17, -0.06f, 0.06f));
            if (hash2(x, y, 55) % 29 == 0) col = col.scaled(0.9f);
            c.set(x, y, col);
        }
    }
}

void paintLogSide(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            const uint32_t h = hash2(x, 0, 61);
            float shade = 1.0f + jitter(x, y, 19, -0.08f, 0.08f);
            if (h % 7 == 0) shade *= 0.78f;
            if (h % 13 == 0) shade *= 1.12f;
            c.set(x, y, hex(0x6B4F2A).scaled(shade));
        }
    }
}

void paintLogTop(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            const float dx = static_cast<float>(x) - 7.5f;
            const float dy = static_cast<float>(y) - 7.5f;
            const float r = std::sqrt(dx * dx + dy * dy);
            const int ring = static_cast<int>(r) % 3;
            Color col = ring == 0 ? hex(0x8A6836) : hex(0x6B4F2A);
            if (r > 7.2f) col = hex(0x4E381D);
            col = col.scaled(1.0f + jitter(x, y, 23, -0.06f, 0.06f));
            c.set(x, y, col);
        }
    }
}

void paintLeaves(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            const uint32_t h = hash2(x, y, 71);
            Color col = hex(0x3F7A2E).scaled(1.0f + jitter(x, y, 29, -0.20f, 0.20f));
            if (h % 5 == 0) col = col.scaled(0.82f);
            if (h % 17 == 0) col = Color{0.0f, 0.0f, 0.0f, 0.0f};
            c.set(x, y, col);
        }
    }
}

void paintWater(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            const float wave = std::sin((static_cast<float>(x) + static_cast<float>(y)) * 0.6f) * 0.06f;
            Color col = hex(0x2F68D0, 0.72f).scaled(1.0f + wave + jitter(x, y, 31, -0.05f, 0.05f));
            c.set(x, y, col);
        }
    }
}

void paintPlanks(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            const int board = y / 4;
            const int seamX = (board % 2 == 0) ? 0 : 8;
            const bool seam = (y % 4 == 0) || (x == seamX);
            Color col = hex(0xB08A52).scaled(1.0f + jitter(x, y, 37, -0.08f, 0.08f));
            if (hash2(x / 3, y, 83) % 9 == 0) col = col.scaled(0.94f);
            if (seam) col = hex(0x7A5B31);
            c.set(x, y, col);
        }
    }
}

void paintBrick(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            const int row = y / 4;
            const int offset = (row % 2 == 0) ? 0 : 4;
            const bool mortar = (y % 4 == 0) || ((x + offset) % 8 == 0);
            Color col = hex(0x9D4B3C).scaled(1.0f + jitter(x, y, 41, -0.10f, 0.10f));
            if (mortar) col = hex(0xB9B0A4);
            c.set(x, y, col);
        }
    }
}

void paintGlass(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            const bool border = x == 0 || y == 0 || x == TILE_PX - 1 || y == TILE_PX - 1;
            Color col = border ? hex(0xBFE3EF, 0.95f) : hex(0xCFEFF8, 0.16f);
            if (!border && (x == y || x == y + 1)) col = hex(0xEAF9FF, 0.45f);
            c.set(x, y, col);
        }
    }
}

void paintSnow(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            Color col = hex(0xF2F6F8).scaled(1.0f + jitter(x, y, 43, -0.03f, 0.02f));
            if (hash2(x, y, 87) % 37 == 0) col = hex(0xDCE9F2);
            c.set(x, y, col);
        }
    }
}

void paintGravel(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            const uint32_t h = hash2(x / 2, y / 2, 97);
            Color col = hex(0x8A8A8A).scaled(0.8f + unitFloat(h) * 0.4f);
            col = col.scaled(1.0f + jitter(x, y, 47, -0.05f, 0.05f));
            c.set(x, y, col);
        }
    }
}

void paintBedrock(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            const uint32_t h = hash2(x / 3, y / 3, 107);
            Color col = hex(0x4A4A4A).scaled(0.55f + unitFloat(h) * 0.8f);
            if (hash2(x, y, 113) % 13 == 0) col = hex(0x232323);
            c.set(x, y, col);
        }
    }
}

void paintSandstone(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            Color col = hex(0xD9C98E).scaled(1.0f + jitter(x, y, 51, -0.05f, 0.05f));
            if (y % 5 == 0) col = col.scaled(0.92f);
            c.set(x, y, col);
        }
    }
}

void paintGlow(Canvas& c) {
    for (int y = 0; y < TILE_PX; ++y) {
        for (int x = 0; x < TILE_PX; ++x) {
            Color col = hex(0xF2C14E).scaled(0.92f + jitter(x, y, 53, -0.05f, 0.10f));
            if (hash2(x, y, 119) % 17 == 0) col = hex(0xFFF0B0);
            c.set(x, y, col);
        }
    }
}

void paintTile(Canvas& c, uint8_t tile) {
    switch (tile) {
        case TILE_GRASS_TOP: paintGrassTop(c); break;
        case TILE_GRASS_SIDE: paintGrassSide(c); break;
        case TILE_DIRT: paintDirt(c); break;
        case TILE_STONE: paintStone(c); break;
        case TILE_COBBLE: paintCobble(c); break;
        case TILE_SAND: paintSand(c); break;
        case TILE_LOG_SIDE: paintLogSide(c); break;
        case TILE_LOG_TOP: paintLogTop(c); break;
        case TILE_LEAVES: paintLeaves(c); break;
        case TILE_WATER: paintWater(c); break;
        case TILE_PLANKS: paintPlanks(c); break;
        case TILE_BRICK: paintBrick(c); break;
        case TILE_GLASS: paintGlass(c); break;
        case TILE_SNOW: paintSnow(c); break;
        case TILE_GRAVEL: paintGravel(c); break;
        case TILE_BEDROCK: paintBedrock(c); break;
        case TILE_SANDSTONE: paintSandstone(c); break;
        case TILE_GLOW: paintGlow(c); break;
        default: c.fill(hex(0xFF00FF)); break;
    }
}

}  // namespace

std::vector<uint8_t> buildTerrainAtlas() {
    std::vector<uint8_t> pixels(static_cast<size_t>(ATLAS_PX) * ATLAS_PX * 4, 0);
    for (int tile = 0; tile < TILE_COUNT; ++tile) {
        Canvas c;
        c.data = pixels.data();
        c.ox = (tile % ATLAS_GRID) * TILE_PX;
        c.oy = (tile / ATLAS_GRID) * TILE_PX;
        paintTile(c, static_cast<uint8_t>(tile));
    }
    return pixels;
}

// 5x7 glyphs, one byte per row, bit 4 is the leftmost pixel.
namespace {
const uint8_t kGlyphSpace[7] = {0, 0, 0, 0, 0, 0, 0};
const uint8_t* glyphFor(char ch);
}

std::vector<uint8_t> buildFontAtlas() {
    std::vector<uint8_t> px(static_cast<size_t>(FONT_PX_W) * FONT_PX_H * 4, 0);
    for (int code = 32; code < 128; ++code) {
        const uint8_t* g = glyphFor(static_cast<char>(code));
        const int cellX = (code % FONT_COLS) * FONT_CELL;
        const int cellY = (code / FONT_COLS) * FONT_CELL;
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                const bool on = (g[row] >> (4 - col)) & 1U;
                if (!on) continue;
                const int x = cellX + col;
                const int y = cellY + 6 - row;  // flip: row 0 is the top row
                uint8_t* p = px.data() + (static_cast<size_t>(y) * FONT_PX_W + x) * 4;
                p[0] = p[1] = p[2] = p[3] = 255;
            }
        }
    }
    return px;
}

namespace {

// Every glyph used by the engine's HUD and splash screen.
const uint8_t kA[7] = {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001};
const uint8_t kB[7] = {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110};
const uint8_t kC[7] = {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110};
const uint8_t kD[7] = {0b11100, 0b10010, 0b10001, 0b10001, 0b10001, 0b10010, 0b11100};
const uint8_t kE[7] = {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111};
const uint8_t kF[7] = {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000};
const uint8_t kG[7] = {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111};
const uint8_t kH[7] = {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001};
const uint8_t kI[7] = {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110};
const uint8_t kJ[7] = {0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100};
const uint8_t kK[7] = {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001};
const uint8_t kL[7] = {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111};
const uint8_t kM[7] = {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001};
const uint8_t kN[7] = {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001};
const uint8_t kO[7] = {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110};
const uint8_t kP[7] = {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000};
const uint8_t kQ[7] = {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101};
const uint8_t kR[7] = {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001};
const uint8_t kS[7] = {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110};
const uint8_t kT[7] = {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100};
const uint8_t kU[7] = {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110};
const uint8_t kV[7] = {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100};
const uint8_t kW[7] = {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010};
const uint8_t kX[7] = {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001};
const uint8_t kY[7] = {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100};
const uint8_t kZ[7] = {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111};

const uint8_t k0[7] = {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110};
const uint8_t k1[7] = {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110};
const uint8_t k2[7] = {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111};
const uint8_t k3[7] = {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110};
const uint8_t k4[7] = {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010};
const uint8_t k5[7] = {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110};
const uint8_t k6[7] = {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110};
const uint8_t k7[7] = {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000};
const uint8_t k8[7] = {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110};
const uint8_t k9[7] = {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100};

const uint8_t kDot[7] = {0, 0, 0, 0, 0, 0b00100, 0b00100};
const uint8_t kComma[7] = {0, 0, 0, 0, 0b00100, 0b00100, 0b01000};
const uint8_t kColon[7] = {0, 0b00100, 0b00100, 0, 0b00100, 0b00100, 0};
const uint8_t kBang[7] = {0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0, 0b00100};
const uint8_t kQuestion[7] = {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0, 0b00100};
const uint8_t kPlus[7] = {0, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0};
const uint8_t kMinus[7] = {0, 0, 0, 0b11111, 0, 0, 0};
const uint8_t kSlash[7] = {0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0, 0};
const uint8_t kPercent[7] = {0b11001, 0b11010, 0b00100, 0b01000, 0b10110, 0b00110, 0};
const uint8_t kLParen[7] = {0b00010, 0b00100, 0b01000, 0b01000, 0b01000, 0b00100, 0b00010};
const uint8_t kRParen[7] = {0b01000, 0b00100, 0b00010, 0b00010, 0b00010, 0b00100, 0b01000};
const uint8_t kLBracket[7] = {0b01110, 0b01000, 0b01000, 0b01000, 0b01000, 0b01000, 0b01110};
const uint8_t kRBracket[7] = {0b01110, 0b00010, 0b00010, 0b00010, 0b00010, 0b00010, 0b01110};
const uint8_t kLess[7] = {0b00010, 0b00100, 0b01000, 0b10000, 0b01000, 0b00100, 0b00010};
const uint8_t kGreater[7] = {0b01000, 0b00100, 0b00010, 0b00001, 0b00010, 0b00100, 0b01000};
const uint8_t kEquals[7] = {0, 0, 0b11111, 0, 0b11111, 0, 0};
const uint8_t kStar[7] = {0, 0b00100, 0b10101, 0b01110, 0b10101, 0b00100, 0};
const uint8_t kQuote[7] = {0b01010, 0b01010, 0, 0, 0, 0, 0};
const uint8_t kHash[7] = {0b01010, 0b01010, 0b11111, 0b01010, 0b11111, 0b01010, 0b01010};
const uint8_t kUnderscore[7] = {0, 0, 0, 0, 0, 0, 0b11111};
const uint8_t kPipe[7] = {0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100};

const uint8_t* glyphFor(char ch) {
    if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    switch (ch) {
        case ' ': return kGlyphSpace;
        case 'A': return kA;
        case 'B': return kB;
        case 'C': return kC;
        case 'D': return kD;
        case 'E': return kE;
        case 'F': return kF;
        case 'G': return kG;
        case 'H': return kH;
        case 'I': return kI;
        case 'J': return kJ;
        case 'K': return kK;
        case 'L': return kL;
        case 'M': return kM;
        case 'N': return kN;
        case 'O': return kO;
        case 'P': return kP;
        case 'Q': return kQ;
        case 'R': return kR;
        case 'S': return kS;
        case 'T': return kT;
        case 'U': return kU;
        case 'V': return kV;
        case 'W': return kW;
        case 'X': return kX;
        case 'Y': return kY;
        case 'Z': return kZ;
        case '0': return k0;
        case '1': return k1;
        case '2': return k2;
        case '3': return k3;
        case '4': return k4;
        case '5': return k5;
        case '6': return k6;
        case '7': return k7;
        case '8': return k8;
        case '9': return k9;
        case '.': return kDot;
        case ',': return kComma;
        case ':': return kColon;
        case '!': return kBang;
        case '?': return kQuestion;
        case '+': return kPlus;
        case '-': return kMinus;
        case '/': return kSlash;
        case '%': return kPercent;
        case '(': return kLParen;
        case ')': return kRParen;
        case '[': return kLBracket;
        case ']': return kRBracket;
        case '<': return kLess;
        case '>': return kGreater;
        case '=': return kEquals;
        case '*': return kStar;
        case '\'': return kQuote;
        case '"': return kQuote;
        case '#': return kHash;
        case '_': return kUnderscore;
        case '|': return kPipe;
        default: return kQuestion;
    }
}

}  // namespace
}  // namespace mcc
