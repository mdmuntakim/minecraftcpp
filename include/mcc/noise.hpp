// Deterministic, allocation free noise. The same seed always produces the
// same world on every device, which is what lets the tests compare against a
// fixed reference.
#pragma once

#include <cmath>
#include <cstdint>

namespace mcc {

inline uint32_t hashU32(uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return v;
}

inline uint32_t hash2(int x, int z, uint32_t seed) {
    const uint32_t h = static_cast<uint32_t>(x) * 0x9e3779b1U ^
                       static_cast<uint32_t>(z) * 0x85ebca77U ^
                       seed * 0xc2b2ae3dU;
    return hashU32(h);
}

inline uint32_t hash3(int x, int y, int z, uint32_t seed) {
    const uint32_t h = static_cast<uint32_t>(x) * 0x9e3779b1U ^
                       static_cast<uint32_t>(y) * 0x27d4eb2fU ^
                       static_cast<uint32_t>(z) * 0x85ebca77U ^
                       seed * 0xc2b2ae3dU;
    return hashU32(h);
}

inline float unitFloat(uint32_t h) {
    return static_cast<float>(h & 0xFFFFFFU) / static_cast<float>(0xFFFFFFU);
}

inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Gradient noise in [-1, 1] (Perlin style gradients from a hash table).
inline float grad2(int ix, int iz, float fx, float fz, uint32_t seed) {
    static const float kGx[8] = {1, -1, 0, 0, 0.7071f, -0.7071f, 0.7071f, -0.7071f};
    static const float kGz[8] = {0, 0, 1, -1, 0.7071f, 0.7071f, -0.7071f, -0.7071f};
    const int g = static_cast<int>(hash2(ix, iz, seed) & 7U);
    return kGx[g] * fx + kGz[g] * fz;
}

inline float noise2(float x, float z, uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iz = static_cast<int>(std::floor(z));
    const float fx = x - static_cast<float>(ix);
    const float fz = z - static_cast<float>(iz);
    const float u = fade(fx);
    const float v = fade(fz);
    const float n00 = grad2(ix, iz, fx, fz, seed);
    const float n10 = grad2(ix + 1, iz, fx - 1.0f, fz, seed);
    const float n01 = grad2(ix, iz + 1, fx, fz - 1.0f, seed);
    const float n11 = grad2(ix + 1, iz + 1, fx - 1.0f, fz - 1.0f, seed);
    return lerp(lerp(n00, n10, u), lerp(n01, n11, u), v);
}

inline float grad3(int ix, int iy, int iz, float fx, float fy, float fz, uint32_t seed) {
    const int g = static_cast<int>(hash3(ix, iy, iz, seed) & 15U);
    const float gx = (g & 1) ? 1.0f : -1.0f;
    const float gy = (g & 2) ? 1.0f : -1.0f;
    const float gz = (g & 4) ? 1.0f : -1.0f;
    const float scale = (g & 8) ? 0.5773f : 0.7071f;
    return (gx * fx + gy * fy + gz * fz) * scale;
}

inline float noise3(float x, float y, float z, uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const int iz = static_cast<int>(std::floor(z));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float fz = z - static_cast<float>(iz);
    const float u = fade(fx), v = fade(fy), w = fade(fz);

    const float n000 = grad3(ix, iy, iz, fx, fy, fz, seed);
    const float n100 = grad3(ix + 1, iy, iz, fx - 1, fy, fz, seed);
    const float n010 = grad3(ix, iy + 1, iz, fx, fy - 1, fz, seed);
    const float n110 = grad3(ix + 1, iy + 1, iz, fx - 1, fy - 1, fz, seed);
    const float n001 = grad3(ix, iy, iz + 1, fx, fy, fz - 1, seed);
    const float n101 = grad3(ix + 1, iy, iz + 1, fx - 1, fy, fz - 1, seed);
    const float n011 = grad3(ix, iy + 1, iz + 1, fx, fy - 1, fz - 1, seed);
    const float n111 = grad3(ix + 1, iy + 1, iz + 1, fx - 1, fy - 1, fz - 1, seed);

    return lerp(lerp(lerp(n000, n100, u), lerp(n010, n110, u), v),
                lerp(lerp(n001, n101, u), lerp(n011, n111, u), v), w);
}

inline float fbm2(float x, float z, int octaves, uint32_t seed) {
    float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += noise2(x * freq, z * freq, seed + static_cast<uint32_t>(i) * 1013U) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

}  // namespace mcc
