// Generates the launcher icon and the launch screen as PNG files at build
// time. The artwork is drawn here, so the repository never contains a copied
// image and the APK never ships Mojang art.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef MCC_HAVE_ZLIB
#include <zlib.h>
#endif

#include "mcc/math.hpp"
#include "mcc/texture.hpp"

using namespace mcc;

namespace {

// ---------------------------------------------------------------- PNG output
uint32_t crcTable[256];
bool crcReady = false;

void initCrc() {
    if (crcReady) return;
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320U ^ (c >> 1) : (c >> 1);
        crcTable[n] = c;
    }
    crcReady = true;
}

uint32_t crc32(const uint8_t* data, size_t len) {
    initCrc();
    uint32_t c = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i) c = crcTable[(c ^ data[i]) & 0xFFU] ^ (c >> 8);
    return c ^ 0xFFFFFFFFU;
}

uint32_t adler32(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521U;
        b = (b + a) % 65521U;
    }
    return (b << 16) | a;
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void putChunk(std::vector<uint8_t>& png, const char type[4], const std::vector<uint8_t>& data) {
    put32(png, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    png.insert(png.end(), body.begin(), body.end());
    put32(png, crc32(body.data(), body.size()));
}

// zlib stream with stored (uncompressed) deflate blocks: no dependency on a
// compression library, the files are small enough.
std::vector<uint8_t> zlibStore(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);
    size_t offset = 0;
    while (offset < raw.size()) {
        const size_t n = std::min<size_t>(65535, raw.size() - offset);
        const bool final = offset + n == raw.size();
        z.push_back(final ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n & 0xFF));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n & 0xFF));
        z.push_back(static_cast<uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<long>(offset),
                 raw.begin() + static_cast<long>(offset + n));
        offset += n;
    }
    put32(z, adler32(raw.data(), raw.size()));
    return z;
}

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;

    Image() = default;
    Image(int width, int height) : w(width), h(height), px(width * height * 4, 0) {}

    void blend(int x, int y, Color c, float coverage = 1.0f) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        const float a = clampf(c.a * coverage, 0.0f, 1.0f);
        if (a <= 0.0f) return;
        uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
        p[0] = static_cast<uint8_t>(clampf(c.r * a + (p[0] / 255.0f) * (1 - a), 0, 1) * 255.0f);
        p[1] = static_cast<uint8_t>(clampf(c.g * a + (p[1] / 255.0f) * (1 - a), 0, 1) * 255.0f);
        p[2] = static_cast<uint8_t>(clampf(c.b * a + (p[2] / 255.0f) * (1 - a), 0, 1) * 255.0f);
        p[3] = static_cast<uint8_t>(clampf(a + (p[3] / 255.0f) * (1 - a), 0, 1) * 255.0f);
    }

    void rect(int x, int y, int rw, int rh, Color c) {
        for (int yy = y; yy < y + rh; ++yy)
            for (int xx = x; xx < x + rw; ++xx) blend(xx, yy, c);
    }

    void gradientV(int x, int y, int rw, int rh, Color top, Color bottom) {
        for (int yy = 0; yy < rh; ++yy) {
            const float t = rh > 1 ? static_cast<float>(yy) / (rh - 1) : 0.0f;
            const Color c{lerpf(top.r, bottom.r, t), lerpf(top.g, bottom.g, t),
                          lerpf(top.b, bottom.b, t), lerpf(top.a, bottom.a, t)};
            for (int xx = 0; xx < rw; ++xx) blend(x + xx, y + yy, c);
        }
    }

    void circle(float cx, float cy, float r, Color c) {
        for (int y = static_cast<int>(cy - r); y <= static_cast<int>(cy + r); ++y) {
            for (int x = static_cast<int>(cx - r); x <= static_cast<int>(cx + r); ++x) {
                const float d = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) +
                                          (y + 0.5f - cy) * (y + 0.5f - cy));
                const float cov = clampf(r - d + 0.5f, 0.0f, 1.0f);
                blend(x, y, c, cov);
            }
        }
    }

    void poly(const std::vector<std::pair<float, float>>& pts, Color c,
              float noiseAmount = 0.0f, uint32_t seed = 1U) {
        float minX = pts[0].first, maxX = pts[0].first;
        float minY = pts[0].second, maxY = pts[0].second;
        for (const auto& p : pts) {
            minX = std::min(minX, p.first);
            maxX = std::max(maxX, p.first);
            minY = std::min(minY, p.second);
            maxY = std::max(maxY, p.second);
        }
        const int x0 = std::max(0, static_cast<int>(minX));
        const int x1 = std::min(w - 1, static_cast<int>(maxX) + 1);
        const int y0 = std::max(0, static_cast<int>(minY));
        const int y1 = std::min(h - 1, static_cast<int>(maxY) + 1);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                int inside = 0;
                float cov = 0.0f;
                for (int sy = 0; sy < 2; ++sy) {
                    for (int sx = 0; sx < 2; ++sx) {
                        const float px = x + 0.25f + sx * 0.5f;
                        const float py = y + 0.25f + sy * 0.5f;
                        bool in = false;
                        for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
                            const auto& a = pts[i];
                            const auto& b = pts[j];
                            if (((a.second > py) != (b.second > py)) &&
                                (px < (b.first - a.first) * (py - a.second) /
                                              (b.second - a.second) +
                                          a.first)) {
                                in = !in;
                            }
                        }
                        inside += in ? 1 : 0;
                        cov += in ? 0.25f : 0.0f;
                    }
                }
                if (inside == 0) continue;
                float f = 1.0f;
                if (noiseAmount > 0.0f) {
                    const uint32_t hsh = (static_cast<uint32_t>(x / 4) * 73856093U) ^
                                         (static_cast<uint32_t>(y / 4) * 19349663U) ^ seed;
                    f += noiseAmount * (static_cast<float>(hsh % 1000U) / 1000.0f - 0.5f) * 2.0f;
                }
                blend(x, y, c.scaled(f), cov);
            }
        }
    }

    void line(float x0, float y0, float x1, float y1, float thickness, Color c) {
        const float dx = x1 - x0, dy = y1 - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) return;
        const float nx = -dy / len * thickness * 0.5f;
        const float ny = dx / len * thickness * 0.5f;
        poly({{x0 + nx, y0 + ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny}},
             c);
    }

    void blur(int radius) {
        std::vector<uint8_t> tmp(px.size());
        const int win = radius * 2 + 1;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int acc[3] = {0, 0, 0};
                for (int k = -radius; k <= radius; ++k) {
                    const int sx = std::min(w - 1, std::max(0, x + k));
                    const uint8_t* p = &px[(static_cast<size_t>(y) * w + sx) * 4];
                    acc[0] += p[0];
                    acc[1] += p[1];
                    acc[2] += p[2];
                }
                uint8_t* o = &tmp[(static_cast<size_t>(y) * w + x) * 4];
                o[0] = static_cast<uint8_t>(acc[0] / win);
                o[1] = static_cast<uint8_t>(acc[1] / win);
                o[2] = static_cast<uint8_t>(acc[2] / win);
                o[3] = 255;
            }
        }
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int acc[3] = {0, 0, 0};
                for (int k = -radius; k <= radius; ++k) {
                    const int sy = std::min(h - 1, std::max(0, y + k));
                    const uint8_t* p = &tmp[(static_cast<size_t>(sy) * w + x) * 4];
                    acc[0] += p[0];
                    acc[1] += p[1];
                    acc[2] += p[2];
                }
                uint8_t* o = &px[(static_cast<size_t>(y) * w + x) * 4];
                o[0] = static_cast<uint8_t>(acc[0] / win);
                o[1] = static_cast<uint8_t>(acc[1] / win);
                o[2] = static_cast<uint8_t>(acc[2] / win);
                o[3] = 255;
            }
        }
    }

    Image scaled(int nw, int nh) const {
        Image out(nw, nh);
        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                const int sx0 = x * w / nw;
                const int sx1 = std::max(sx0 + 1, (x + 1) * w / nw);
                const int sy0 = y * h / nh;
                const int sy1 = std::max(sy0 + 1, (y + 1) * h / nh);
                int acc[4] = {0, 0, 0, 0};
                int n = 0;
                for (int sy = sy0; sy < sy1; ++sy) {
                    for (int sx = sx0; sx < sx1; ++sx) {
                        const uint8_t* p = &px[(static_cast<size_t>(sy) * w + sx) * 4];
                        acc[0] += p[0];
                        acc[1] += p[1];
                        acc[2] += p[2];
                        acc[3] += p[3];
                        ++n;
                    }
                }
                uint8_t* o = &out.px[(static_cast<size_t>(y) * nw + x) * 4];
                o[0] = static_cast<uint8_t>(acc[0] / n);
                o[1] = static_cast<uint8_t>(acc[1] / n);
                o[2] = static_cast<uint8_t>(acc[2] / n);
                o[3] = static_cast<uint8_t>(acc[3] / n);
            }
        }
        return out;
    }

    bool writePng(const std::string& path) const {
        std::vector<uint8_t> raw;
        raw.reserve(static_cast<size_t>(w) * h * 4 + h);
        for (int y = 0; y < h; ++y) {
            raw.push_back(0);  // filter: none
            const uint8_t* row = &px[static_cast<size_t>(y) * w * 4];
            raw.insert(raw.end(), row, row + static_cast<size_t>(w) * 4);
        }
        std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        std::vector<uint8_t> ihdr;
        put32(ihdr, static_cast<uint32_t>(w));
        put32(ihdr, static_cast<uint32_t>(h));
        ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
        putChunk(png, "IHDR", ihdr);
#ifdef MCC_HAVE_ZLIB
        uLongf bound = compressBound(static_cast<uLong>(raw.size()));
        std::vector<uint8_t> packed(bound);
        if (compress2(packed.data(), &bound, raw.data(), static_cast<uLong>(raw.size()), 9) != Z_OK) {
            return false;
        }
        packed.resize(bound);
        putChunk(png, "IDAT", packed);
#else
        putChunk(png, "IDAT", zlibStore(raw));
#endif
        putChunk(png, "IEND", {});
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return false;
        const size_t written = std::fwrite(png.data(), 1, png.size(), f);
        std::fclose(f);
        return written == png.size();
    }
};

// ------------------------------------------------------------------- drawing
const std::vector<uint8_t>& fontAtlas() {
    static const std::vector<uint8_t> font = buildFontAtlas();
    return font;
}

void drawText(Image& img, const std::string& text, int x, int y, int scale, Color color) {
    const std::vector<uint8_t>& font = fontAtlas();
    int cx = x;
    for (char chRaw : text) {
        char ch = chRaw;
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        const int code = static_cast<unsigned char>(ch);
        const int cellX = (code % FONT_COLS) * FONT_CELL;
        const int cellY = (code / FONT_COLS) * FONT_CELL;
        for (int gy = 0; gy < FONT_CELL; ++gy) {
            for (int gx = 0; gx < FONT_CELL; ++gx) {
                const int ay = cellY + (FONT_CELL - 2 - gy);
                const int ax = cellX + gx;
                const size_t idx = (static_cast<size_t>(ay) * FONT_PX_W + ax) * 4;
                const uint8_t a = font[idx + 3];
                if (!a) continue;
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        img.blend(cx + gx * scale + sx, y + gy * scale + sy, color,
                                  a / 255.0f);
                    }
                }
            }
        }
        cx += 6 * scale;
    }
}

int textWidth(const std::string& text, int scale) {
    return static_cast<int>(text.size()) * 6 * scale - scale;
}

void drawTextCentered(Image& img, const std::string& text, int y, int scale, Color color) {
    drawText(img, text, (img.w - textWidth(text, scale)) / 2, y, scale, color);
}

void drawIsoCube(Image& img, float cx, float cy, float half, float side, bool grass) {
    const float topH = half * 0.55f;
    const Color top = grass ? Color::rgb(0x67a83f) : Color::rgb(0x8d6e63);
    const Color left = grass ? Color::rgb(0x8b5e3c) : Color::rgb(0x9c7b58);
    const Color right = grass ? Color::rgb(0x74502f) : Color::rgb(0x7d6047);

    img.poly({{cx - half, cy}, {cx, cy + topH}, {cx + half, cy}, {cx, cy - topH}}, top,
             0.10f, 3U);
    img.poly({{cx - half, cy}, {cx, cy + topH}, {cx, cy + topH + side},
              {cx - half, cy + side}},
             left, 0.10f, 5U);
    img.poly({{cx + half, cy}, {cx, cy + topH}, {cx, cy + topH + side},
              {cx + half, cy + side}},
             right, 0.10f, 7U);

    // Grass overhang on the two front faces.
    if (grass) {
        const float lip = side * 0.16f;
        img.poly({{cx - half, cy}, {cx, cy + topH}, {cx, cy + topH + lip},
                  {cx - half, cy + lip}},
                 Color::rgb(0x6fa845), 0.08f, 9U);
        img.poly({{cx + half, cy}, {cx, cy + topH}, {cx, cy + topH + lip},
                  {cx + half, cy + lip}},
                 Color::rgb(0x5e8f3b), 0.08f, 11U);
    }

    const Color outline = Color::rgb(0x2a2119, 0.75f);
    img.line(cx - half, cy, cx, cy - topH, 2.0f, outline);
    img.line(cx + half, cy, cx, cy - topH, 2.0f, outline);
    img.line(cx - half, cy, cx, cy + topH + side, 2.0f, outline);
    img.line(cx + half, cy, cx, cy + topH + side, 2.0f, outline);
    img.line(cx - half, cy, cx - half, cy + side, 2.0f, outline);
    img.line(cx + half, cy, cx + half, cy + side, 2.0f, outline);
}

void drawBlockBackground(Image& img) {
    // A grid of blocky terrain, blurred so the icon has depth behind the cube.
    const int cell = img.w / 8;
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            const uint32_t h = (static_cast<uint32_t>(row) * 2654435761U) ^
                               (static_cast<uint32_t>(col) * 40503U);
            const bool sky = row < 2;
            Color c;
            if (sky) {
                c = Color::rgb(0x8ec9f0);
            } else if (row < 4) {
                c = Color::rgb(0x6aa84f).scaled(0.9f + (h % 100) / 500.0f);
            } else {
                c = Color::rgb(0x8b5e3c).scaled(0.85f + (h % 100) / 400.0f);
            }
            img.rect(col * cell, row * cell, cell, cell, c);
        }
    }
    img.blur(9);

    // 20 percent of the sharp layer stays, which keeps a faint block pattern.
    Image sharp = img;
    for (int y = 0; y < img.h; ++y) {
        for (int x = 0; x < img.w; ++x) {
            const size_t i = (static_cast<size_t>(y) * img.w + x) * 4;
            for (int ch = 0; ch < 3; ++ch) {
                img.px[i + ch] = static_cast<uint8_t>(
                    img.px[i + ch] * 0.8f + sharp.px[i + ch] * 0.2f);
            }
        }
    }
}

Image makeIcon(int size) {
    Image img(size, size);
    drawBlockBackground(img);

    // Darken the middle a little so the lettering reads.
    img.circle(size * 0.5f, size * 0.52f, size * 0.42f, Color::rgb(0x101820, 0.20f));
    img.circle(size * 0.5f, size * 0.55f, size * 0.26f, Color::rgb(0x101820, 0.28f));
    drawIsoCube(img, size * 0.5f, size * 0.40f, size * 0.30f, size * 0.30f, true);

    // "C++" with a 3D extrusion, centred in the cube.
    const int scale = std::max(3, size / 38);
    const std::string letters = "C++";
    const int tw = textWidth(letters, scale);
    const int tx = (size - tw) / 2;
    const int ty = static_cast<int>(size * 0.55f) - 3 * scale;
    for (int depth = std::max(2, size / 30); depth >= 1; --depth) {
        drawText(img, letters, tx + depth, ty + depth, scale,
                 Color::rgb(0x30240f, 0.95f));
    }
    drawText(img, letters, tx, ty, scale, Color::rgb(0xfff2b0));
    drawText(img, letters, tx, ty, scale, Color::rgb(0xffffff, 0.22f));

    // Rounded corner mask.
    Image out(size, size);
    const float r = size * 0.18f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            float dx = 0.0f, dy = 0.0f;
            if (px < r) dx = r - px;
            else if (px > size - r) dx = px - (size - r);
            if (py < r) dy = r - py;
            else if (py > size - r) dy = py - (size - r);
            const float d = std::sqrt(dx * dx + dy * dy);
            const float cov = clampf(r - d + 0.5f, 0.0f, 1.0f);
            const size_t src = (static_cast<size_t>(y) * size + x) * 4;
            const size_t dst = src;
            out.px[dst + 0] = img.px[src + 0];
            out.px[dst + 1] = img.px[src + 1];
            out.px[dst + 2] = img.px[src + 2];
            out.px[dst + 3] = static_cast<uint8_t>(cov * 255.0f);
        }
    }
    return out;
}

Image makeSplash(int w, int h) {
    Image img(w, h);
    const float s = static_cast<float>(std::min(w, h));
    img.gradientV(0, 0, w, h, Color::rgb(0x3f7fd0), Color::rgb(0x9fd0e8));

    // Sun.
    img.circle(w * 0.80f, h * 0.16f, s * 0.075f, Color::rgb(0xfff6c8, 0.9f));
    img.circle(w * 0.80f, h * 0.16f, s * 0.050f, Color::rgb(0xffffff));

    // Blocky clouds.
    for (int i = 0; i < 5; ++i) {
        const int cx = static_cast<int>(w * (0.06f + 0.16f * i));
        const int cy = static_cast<int>(h * (0.10f + 0.04f * (i % 3)));
        const int cw = static_cast<int>(s * 0.18f);
        const int chh = static_cast<int>(s * 0.035f);
        img.rect(cx, cy, cw, chh, Color::rgb(0xffffff, 0.55f));
        img.rect(cx + cw / 5, cy - chh / 2, cw / 2, chh, Color::rgb(0xffffff, 0.55f));
    }

    // Terrain silhouette near the bottom.
    const int horizon = static_cast<int>(h * 0.86f);
    const int step = std::max(8, w / 40);
    for (int x = 0; x < w; x += step) {
        const uint32_t hh = (static_cast<uint32_t>(x) * 2246822519U) % 1000U;
        const int height = static_cast<int>(s * (0.02f + 0.08f * (hh / 1000.0f)));
        const int top = horizon - height;
        const bool green = (hh % 3) != 0;
        img.rect(x, top, step + 1, h - top,
                 green ? Color::rgb(0x5e8f3b) : Color::rgb(0x8b5e3c));
        img.rect(x, top, step + 1, std::max(3, h / 240),
                 green ? Color::rgb(0x6fa845) : Color::rgb(0xd9cf9a));
    }
    img.rect(0, horizon, w, h - horizon, Color::rgb(0x7b5a3a));
    for (int y = horizon; y < h; y += std::max(6, h / 50)) {
        img.rect(0, y, w, std::max(1, h / 480), Color::rgb(0x6a4b2f, 0.45f));
    }

    // Isometric block logo.
    const float cubeCx = w * 0.5f;
    const float cubeCy = h * 0.30f;
    drawIsoCube(img, cubeCx, cubeCy, s * 0.145f, s * 0.145f, true);

    const int titleScale = std::max(3, static_cast<int>(s / 95.0f));
    const std::string title = "MINECRAFTC++";
    const std::string subtitle = "C++ VOXEL ENGINE - ARM64";
    const std::string tagline = "CLEAN BUILD - NOT AFFILIATED WITH MOJANG";
    const int titleY = static_cast<int>(h * 0.54f);
    drawTextCentered(img, title, titleY, titleScale, Color::rgb(0xffffff));
    drawTextCentered(img, subtitle, titleY + 10 * titleScale,
                     std::max(2, titleScale / 2), Color::rgb(0xe8f4ff));
    drawTextCentered(img, tagline, static_cast<int>(h * 0.93f),
                     std::max(2, static_cast<int>(s / 300.0f)),
                     Color::rgb(0xffffff, 0.85f));

    // Loading bar frame, filled by the engine once it is running.
    const int barW = static_cast<int>(w * 0.42f);
    const int barH = static_cast<int>(s * 0.018f);
    const int barX = (w - barW) / 2;
    const int barY = static_cast<int>(h * 0.72f);
    img.rect(barX - 3, barY - 3, barW + 6, barH + 6, Color::rgb(0x1a1a1a, 0.7f));
    img.rect(barX, barY, static_cast<int>(barW * 0.35f), barH, Color::rgb(0x7fd44a));
    return img;
}

bool mkdirs(const std::string& path) {
    std::string cmd = "mkdir -p '" + path + "'";
    return std::system(cmd.c_str()) == 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string out = "generated";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out = argv[++i];
        }
    }

    mkdirs(out + "/drawable");
    mkdirs(out + "/mipmap-mdpi");
    mkdirs(out + "/mipmap-hdpi");
    mkdirs(out + "/mipmap-xhdpi");
    mkdirs(out + "/mipmap-xxhdpi");
    mkdirs(out + "/mipmap-xxxhdpi");

    const Image icon512 = makeIcon(512);
    struct Density {
        const char* dir;
        int size;
    } densities[] = {
        {"mipmap-mdpi", 48}, {"mipmap-hdpi", 72}, {"mipmap-xhdpi", 96},
        {"mipmap-xxhdpi", 144}, {"mipmap-xxxhdpi", 192},
    };
    for (const Density& d : densities) {
        const Image scaled = icon512.scaled(d.size, d.size);
        const std::string path = out + "/" + d.dir + "/ic_launcher.png";
        if (!scaled.writePng(path)) {
            std::fprintf(stderr, "failed to write %s\n", path.c_str());
            return 1;
        }
    }

    if (!icon512.scaled(256, 256).writePng(out + "/drawable/icon.png")) return 1;
    if (!makeSplash(1280, 720).writePng(out + "/drawable/splash.png")) return 1;
    std::printf("wrote icon and splash into %s\n", out.c_str());
    return 0;
}
