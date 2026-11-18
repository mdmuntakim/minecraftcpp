#include "renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "glutil.hpp"
#include "mcc/block.hpp"
#include "mcc/texture.hpp"

namespace mcc {
namespace {

constexpr int kUiAtlas = 0;

const char* kTerrainVs = R"GLSL(
#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in float aTile;
layout(location = 3) in float aLight;
uniform mat4 uVP;
uniform vec3 uOffset;
uniform vec3 uCamPos;
out vec2 vUV;
out float vLight;
out float vFog;
void main() {
    vec3 world = aPos + uOffset;
    float col = mod(aTile, 16.0);
    float row = floor(aTile / 16.0);
    vUV = (vec2(col, row) + aUV * 0.92 + 0.04) / 16.0;
    vLight = aLight;
    vFog = distance(world, uCamPos);
    gl_Position = uVP * vec4(world, 1.0);
}
)GLSL";

const char* kTerrainFs = R"GLSL(
#version 300 es
precision highp float;
in vec2 vUV;
in float vLight;
in float vFog;
uniform sampler2D uAtlas;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uAlphaTest;
uniform vec4 uTint;
out vec4 outColor;
void main() {
    vec4 texel = texture(uAtlas, vUV) * uTint;
    if (uAlphaTest > 0.5 && texel.a < 0.4) discard;
    float fog = clamp((vFog - uFogStart) / max(uFogEnd - uFogStart, 0.001), 0.0, 1.0);
    outColor = vec4(mix(texel.rgb * vLight, uFogColor, fog), texel.a);
}
)GLSL";

const char* kUiVs = R"GLSL(
#version 300 es
layout(location = 0) in vec2 aCorner;
uniform vec2 uScreen;
uniform vec2 uP0;
uniform vec2 uP1;
uniform vec2 uP2;
uniform vec2 uP3;
out vec2 vUV;
void main() {
    vec2 p = mix(mix(uP0, uP1, aCorner.x), mix(uP3, uP2, aCorner.x), aCorner.y);
    gl_Position = vec4(p.x / uScreen.x * 2.0 - 1.0,
                       1.0 - p.y / uScreen.y * 2.0, 0.0, 1.0);
    vUV = aCorner;
}
)GLSL";

const char* kUiFs = R"GLSL(
#version 300 es
precision mediump float;
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uUv;
uniform vec4 uColor;
uniform float uCircle;
out vec4 outColor;
void main() {
    vec2 uv = mix(uUv.xy, uUv.zw, vUV);
    vec4 c = texture(uTex, uv) * uColor;
    if (uCircle > 0.5) {
        float d = distance(vUV, vec2(0.5));
        c.a *= 1.0 - smoothstep(0.46, 0.5, d);
    }
    outColor = c;
}
)GLSL";

inline Color skyTop() { return Color::rgb(0x3d7fc4); }
inline Color skyHorizon() { return Color::rgb(0xbfe0f2); }
inline Vec3 sunDirection() { return Vec3{0.42f, 0.66f, -0.62f}.normalized(); }

uint64_t chunkKey(int cx, int cz) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
           static_cast<uint32_t>(cz);
}

uint32_t seamHash(const World& world, int cx, int cz) {
    uint32_t h = 2166136261U;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            h ^= world.revision(cx + dx, cz + dz);
            h *= 16777619U;
        }
    }
    return h;
}

void setAttribPointers() {
    const GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(sizeof(float) * 3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(sizeof(float) * 5));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(sizeof(float) * 6));
}

}  // namespace

void Renderer::GpuMesh::release() {
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ibo) glDeleteBuffers(1, &ibo);
    if (vao) glDeleteVertexArrays(1, &vao);
    vao = vbo = ibo = 0;
    count = 0;
}

namespace {
void uploadMeshImpl(Renderer::GpuMesh& mesh, const std::vector<Vertex>& verts,
                    const std::vector<uint32_t>& indices) {
    if (indices.empty()) {
        mesh.count = 0;
        return;
    }
    if (!mesh.vao) {
        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.vbo);
        glGenBuffers(1, &mesh.ibo);
    }
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)), verts.data(),
                 GL_STATIC_DRAW);
    setAttribPointers();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                 indices.data(), GL_STATIC_DRAW);
    mesh.count = static_cast<GLsizei>(indices.size());
    glBindVertexArray(0);
}
}  // namespace

void UiLayout::compute(int w, int h, bool flying) {
    width = w;
    height = h;
    scale = clampf(static_cast<float>(h) / 720.0f, 0.7f, 3.0f);
    const float s = scale;

    joyR = 84.0f * s;
    joyCx = 112.0f * s;
    joyCy = static_cast<float>(h) - 118.0f * s;

    const float r = 44.0f * s;
    jump = {static_cast<float>(w) - 74.0f * s - r,
            static_cast<float>(h) - 96.0f * s - r, 2 * r, 2 * r};
    mine = {jump.x - 104.0f * s, jump.y, 2 * r, 2 * r};
    place = {mine.x - 104.0f * s, jump.y, 2 * r, 2 * r};
    fly = {jump.x, jump.y - 104.0f * s, 2 * r, 2 * r};

    const float fr = 30.0f * s;
    flyUp = {jump.x + r - fr, jump.y - 200.0f * s - fr, 2 * fr, 2 * fr};
    flyDown = {jump.x + r - fr, jump.y - 276.0f * s - fr, 2 * fr, 2 * fr};
    if (flying) flyDown.x = flyUp.x;
    (void)flying;

    const float slot = 46.0f * s;
    const float gap = 5.0f * s;
    const float total = 9 * slot + 8 * gap;
    const float x0 = (static_cast<float>(w) - total) * 0.5f;
    const float y0 = static_cast<float>(h) - slot - 10.0f * s;
    for (int i = 0; i < 9; ++i) {
        hotbar[i] = {x0 + i * (slot + gap), y0, slot, slot};
    }
}

bool Renderer::init(int width, int height, std::string* error) {
    width_ = width;
    height_ = height;
    uiScale_ = clampf(static_cast<float>(height) / 720.0f, 0.7f, 3.0f);

    progTerrain_ = linkProgram(kTerrainVs, kTerrainFs, error);
    if (!progTerrain_) return false;
    progUi_ = linkProgram(kUiVs, kUiFs, error);
    if (!progUi_) return false;

    tVP_ = glGetUniformLocation(progTerrain_, "uVP");
    tOffset_ = glGetUniformLocation(progTerrain_, "uOffset");
    tCamPos_ = glGetUniformLocation(progTerrain_, "uCamPos");
    tAtlas_ = glGetUniformLocation(progTerrain_, "uAtlas");
    tFogColor_ = glGetUniformLocation(progTerrain_, "uFogColor");
    tFogStart_ = glGetUniformLocation(progTerrain_, "uFogStart");
    tFogEnd_ = glGetUniformLocation(progTerrain_, "uFogEnd");
    tAlphaTest_ = glGetUniformLocation(progTerrain_, "uAlphaTest");
    tTint_ = glGetUniformLocation(progTerrain_, "uTint");

    uScreen_ = glGetUniformLocation(progUi_, "uScreen");
    uP0_ = glGetUniformLocation(progUi_, "uP0");
    uP1_ = glGetUniformLocation(progUi_, "uP1");
    uP2_ = glGetUniformLocation(progUi_, "uP2");
    uP3_ = glGetUniformLocation(progUi_, "uP3");
    uUv_ = glGetUniformLocation(progUi_, "uUv");
    uColor_ = glGetUniformLocation(progUi_, "uColor");
    uCircle_ = glGetUniformLocation(progUi_, "uCircle");
    uTex_ = glGetUniformLocation(progUi_, "uTex");

    // Terrain atlas.
    const std::vector<uint8_t> atlas = buildTerrainAtlas();
    glGenTextures(1, &atlasTex_);
    glBindTexture(GL_TEXTURE_2D, atlasTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ATLAS_PX, ATLAS_PX, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Bitmap font.
    const std::vector<uint8_t> font = buildFontAtlas();
    glGenTextures(1, &fontTex_);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FONT_PX_W, FONT_PX_H, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, font.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 1x1 white texture for solid quads.
    const uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &whiteTex_);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Unit quad for the UI shader.
    const float corners[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    glGenVertexArrays(1, &uiVao_);
    glGenBuffers(1, &uiVbo_);
    glBindVertexArray(uiVao_);
    glBindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    uiReady_ = true;

    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glViewport(0, 0, width_, height_);
    return true;
}

void Renderer::shutdown() {
    for (auto& kv : chunks_) {
        kv.second.opaque.release();
        kv.second.water.release();
    }
    chunks_.clear();
    if (uiVbo_) glDeleteBuffers(1, &uiVbo_);
    if (uiVao_) glDeleteVertexArrays(1, &uiVao_);
    if (atlasTex_) glDeleteTextures(1, &atlasTex_);
    if (fontTex_) glDeleteTextures(1, &fontTex_);
    if (whiteTex_) glDeleteTextures(1, &whiteTex_);
    if (progTerrain_) glDeleteProgram(progTerrain_);
    if (progUi_) glDeleteProgram(progUi_);
    uiVao_ = uiVbo_ = atlasTex_ = fontTex_ = whiteTex_ = 0;
    progTerrain_ = progUi_ = 0;
    uiReady_ = false;
}

void Renderer::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    width_ = width;
    height_ = height;
    uiScale_ = clampf(static_cast<float>(height) / 720.0f, 0.7f, 3.0f);
    glViewport(0, 0, width_, height_);
}

void Renderer::releaseChunk(GpuChunk& g) {
    g.opaque.release();
    g.water.release();
}

void Renderer::buildChunk(Game& game, int cx, int cz, const Chunk& chunk) {
    buildChunkMesh(game.world(), chunk, staging_);
    const uint64_t key = chunkKey(cx, cz);
    GpuChunk& gpu = chunks_[key];
    uploadMeshImpl(gpu.opaque, staging_.opaqueVerts, staging_.opaqueIndices);
    uploadMeshImpl(gpu.water, staging_.waterVerts, staging_.waterIndices);
    gpu.revision = chunk.revision;
    gpu.seam = seamHash(game.world(), cx, cz);
}

void Renderer::updateWorld(Game& game, int radius, float maxMilliseconds) {
    const auto start = std::chrono::steady_clock::now();
    World& world = game.world();

    // One extra ring of chunks is generated so the seam faces of the visible
    // ring can be culled against real blocks instead of air.
    game.streamChunks(radius + 1);

    const int pcx = static_cast<int>(std::floor(game.player().pos.x)) / CHUNK_SX;
    const int pcz = static_cast<int>(std::floor(game.player().pos.z)) / CHUNK_SZ;

    struct Dirty {
        int cx, cz;
        float dist2;
    };
    std::vector<Dirty> dirty;
    dirty.reserve(static_cast<size_t>((2 * radius + 1) * (2 * radius + 1)));

    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int cx = pcx + dx;
            const int cz = pcz + dz;
            const Chunk* chunk = world.findChunk(cx, cz);
            if (!chunk) continue;
            auto it = chunks_.find(chunkKey(cx, cz));
            const uint32_t seam = seamHash(world, cx, cz);
            const bool stale = it == chunks_.end() || it->second.revision != chunk->revision ||
                               it->second.seam != seam;
            if (stale) {
                dirty.push_back({cx, cz, static_cast<float>(dx * dx + dz * dz)});
            }
        }
    }
    std::sort(dirty.begin(), dirty.end(), [](const Dirty& a, const Dirty& b) {
        return a.dist2 < b.dist2;
    });

    const bool unlimited = maxMilliseconds <= 0.0f;
    for (const Dirty& d : dirty) {
        if (!unlimited) {
            const double elapsed =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                         start)
                    .count();
            if (elapsed > maxMilliseconds) break;
        }
        const Chunk* chunk = world.findChunk(d.cx, d.cz);
        if (chunk) buildChunk(game, d.cx, d.cz, *chunk);
    }

    // Drop meshes that left the streaming window.
    for (auto it = chunks_.begin(); it != chunks_.end();) {
        const int cx = static_cast<int>(static_cast<int32_t>(it->first >> 32));
        const int cz = static_cast<int>(static_cast<int32_t>(it->first & 0xFFFFFFFFU));
        if (std::abs(cx - pcx) > radius + 2 || std::abs(cz - pcz) > radius + 2) {
            releaseChunk(it->second);
            it = chunks_.erase(it);
        } else {
            ++it;
        }
    }

    // Progress and per-frame culling distances.
    readyChunks_ = 0;
    requiredChunks_ = 0;
    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int cx = pcx + dx;
            const int cz = pcz + dz;
            const Chunk* chunk = world.findChunk(cx, cz);
            if (!chunk) continue;
            ++requiredChunks_;
            auto it = chunks_.find(chunkKey(cx, cz));
            if (it == chunks_.end()) continue;
            if (it->second.revision != chunk->revision ||
                it->second.seam != seamHash(world, cx, cz)) {
                continue;
            }
            it->second.dist2 = static_cast<float>(dx * dx + dz * dz);
            ++readyChunks_;
        }
    }
}

void Renderer::draw(Game& game, const HudState& hud, float time) {
    ++frame_;
    glViewport(0, 0, width_, height_);
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (hud.splash) {
        drawSplash(hud, time);
        return;
    }

    const Player& p = game.player();
    const Vec3 eye = p.eye();
    const Vec3 forward = p.forward();
    const Mat4 proj = Mat4::perspective(1.2566f, static_cast<float>(width_) / height_, 0.08f,
                                        1200.0f);
    const Mat4 view = Mat4::lookAt(eye, eye + forward, Vec3{0, 1, 0});
    const Mat4 vp = Mat4::multiply(proj, view);

    drawSky(vp, eye, time);
    drawTerrain(game, 5, time);
    drawHud(game, hud, time);
}

void Renderer::drawSky(const Mat4& vp, const Vec3& eye, float time) {
    (void)time;
    beginUi();
    // Two overlapping gradient bands give a soft horizon.
    const int bands = 16;
    for (int i = 0; i < bands; ++i) {
        const float t0 = static_cast<float>(i) / bands;
        const float t1 = static_cast<float>(i + 1) / bands;
        const Color bottom = Color{lerpf(skyTop().r, skyHorizon().r, t1),
                                   lerpf(skyTop().g, skyHorizon().g, t1),
                                   lerpf(skyTop().b, skyHorizon().b, t1), 1.0f};
        uiTex(whiteTex_);
        // vertical gradient inside one quad is not possible with the plain
        // shader, so each band is a flat colour: 16 bands look smooth enough.
        uiQuad(0.0f, static_cast<float>(height_) * t0, static_cast<float>(width_),
               static_cast<float>(height_) / bands + 1.0f, bottom);
    }

    // The sun as a screen-space disc, projected from a fixed world direction.
    const Vec3 sunWorld = eye + sunDirection() * 600.0f;
    const float* m = vp.m;
    const float cx = m[0] * sunWorld.x + m[4] * sunWorld.y + m[8] * sunWorld.z + m[12];
    const float cy = m[1] * sunWorld.x + m[5] * sunWorld.y + m[9] * sunWorld.z + m[13];
    const float cw = m[3] * sunWorld.x + m[7] * sunWorld.y + m[11] * sunWorld.z + m[15];
    if (cw > 0.05f) {
        const float sx = (cx / cw * 0.5f + 0.5f) * width_;
        const float sy = (1.0f - (cy / cw * 0.5f + 0.5f)) * height_;
        if (sx > -200 && sx < width_ + 200 && sy > -200 && sy < height_ + 200) {
            uiTex(whiteTex_);
            uiCircle(sx, sy, 58.0f * uiScale_, Color::rgb(0xfff6c0, 0.22f));
            uiCircle(sx, sy, 38.0f * uiScale_, Color::rgb(0xfffbdc, 0.95f));
        }
    }
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

void Renderer::drawTerrain(Game& game, int radius, float time) {
    (void)time;
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    const Player& p = game.player();
    const Vec3 eye = p.eye();
    const Mat4 proj = Mat4::perspective(1.2566f, static_cast<float>(width_) / height_, 0.08f,
                                        static_cast<float>(radius) * 16.0f + 190.0f);
    const Mat4 view = Mat4::lookAt(eye, eye + p.forward(), Vec3{0, 1, 0});
    const Mat4 vp = Mat4::multiply(proj, view);
    Frustum frustum;
    frustum.extract(vp);

    struct Item {
        GpuChunk* mesh;
        int cx, cz;
        float dist2;
    };
    std::vector<Item> opaque, transparent;
    opaque.reserve(chunks_.size());
    transparent.reserve(chunks_.size());

    int pcx = static_cast<int>(std::floor(p.pos.x)) / CHUNK_SX;
    int pcz = static_cast<int>(std::floor(p.pos.z)) / CHUNK_SZ;
    for (auto& kv : chunks_) {
        GpuChunk& g = kv.second;
        const int cx = static_cast<int>(static_cast<int32_t>(kv.first >> 32));
        const int cz = static_cast<int>(static_cast<int32_t>(kv.first & 0xFFFFFFFFU));
        if (std::abs(cx - pcx) > radius || std::abs(cz - pcz) > radius) continue;
        const Vec3 mn{static_cast<float>(cx * CHUNK_SX), 0.0f,
                      static_cast<float>(cz * CHUNK_SZ)};
        const Vec3 mx{mn.x + CHUNK_SX, static_cast<float>(WORLD_H), mn.z + CHUNK_SZ};
        if (!frustum.boxVisible(mn, mx)) continue;
        const float dx = mn.x + 8.0f - eye.x;
        const float dz = mn.z + 8.0f - eye.z;
        const float d2 = dx * dx + dz * dz;
        if (g.opaque.count > 0) opaque.push_back({&g, cx, cz, d2});
        if (g.water.count > 0) transparent.push_back({&g, cx, cz, d2});
    }
    std::sort(opaque.begin(), opaque.end(),
              [](const Item& a, const Item& b) { return a.dist2 < b.dist2; });
    std::sort(transparent.begin(), transparent.end(),
              [](const Item& a, const Item& b) { return a.dist2 > b.dist2; });

    glUseProgram(progTerrain_);
    glActiveTexture(GL_TEXTURE0 + kUiAtlas);
    glBindTexture(GL_TEXTURE_2D, atlasTex_);
    glUniform1i(tAtlas_, kUiAtlas);
    glUniformMatrix4fv(tVP_, 1, GL_FALSE, vp.m);
    glUniform3f(tCamPos_, eye.x, eye.y, eye.z);
    const Color fog = skyHorizon();
    glUniform3f(tFogColor_, fog.r, fog.g, fog.b);
    glUniform1f(tFogStart_, static_cast<float>(radius) * 16.0f * 0.45f);
    glUniform1f(tFogEnd_, static_cast<float>(radius) * 16.0f * 0.95f);
    glUniform4f(tTint_, 1.0f, 1.0f, 1.0f, 1.0f);

    drawnFaces_ = 0;
    glUniform1f(tAlphaTest_, 1.0f);
    for (const Item& it : opaque) {
        glUniform3f(tOffset_, static_cast<float>(it.cx * CHUNK_SX), 0.0f,
                    static_cast<float>(it.cz * CHUNK_SZ));
        glBindVertexArray(it.mesh->opaque.vao);
        glDrawElements(GL_TRIANGLES, it.mesh->opaque.count, GL_UNSIGNED_INT, nullptr);
        drawnFaces_ += static_cast<size_t>(it.mesh->opaque.count / 6);
    }

    if (!transparent.empty()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glUniform1f(tAlphaTest_, 0.0f);
        for (const Item& it : transparent) {
            glUniform3f(tOffset_, static_cast<float>(it.cx * CHUNK_SX), 0.0f,
                        static_cast<float>(it.cz * CHUNK_SZ));
            glBindVertexArray(it.mesh->water.vao);
            glDrawElements(GL_TRIANGLES, it.mesh->water.count, GL_UNSIGNED_INT, nullptr);
            drawnFaces_ += static_cast<size_t>(it.mesh->water.count / 6);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_CULL_FACE);
}

void Renderer::beginUi() {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(progUi_);
    glUniform2f(uScreen_, static_cast<float>(width_), static_cast<float>(height_));
    glUniform1i(uTex_, kUiAtlas);
    glBindVertexArray(uiVao_);
    glActiveTexture(GL_TEXTURE0 + kUiAtlas);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
}

void Renderer::uiTex(GLuint tex) { glBindTexture(GL_TEXTURE_2D, tex); }

void Renderer::uiQuad(float x, float y, float w, float h, Color color) {
    uiQuadUv(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, color);
}

void Renderer::uiQuadUv(float x, float y, float w, float h, float u0, float v0, float u1,
                        float v1, Color color) {
    glUniform2f(uP0_, x, y);
    glUniform2f(uP1_, x + w, y);
    glUniform2f(uP2_, x + w, y + h);
    glUniform2f(uP3_, x, y + h);
    glUniform4f(uUv_, u0, v0, u1, v1);
    glUniform4f(uColor_, color.r, color.g, color.b, color.a);
    glUniform1f(uCircle_, 0.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::uiCorners(const float pts[4][2], Color color, bool circle) {
    glUniform2f(uP0_, pts[0][0], pts[0][1]);
    glUniform2f(uP1_, pts[1][0], pts[1][1]);
    glUniform2f(uP2_, pts[2][0], pts[2][1]);
    glUniform2f(uP3_, pts[3][0], pts[3][1]);
    glUniform4f(uUv_, 0, 0, 1, 1);
    glUniform4f(uColor_, color.r, color.g, color.b, color.a);
    glUniform1f(uCircle_, circle ? 1.0f : 0.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::uiCircle(float cx, float cy, float r, Color color) {
    glUniform2f(uP0_, cx - r, cy - r);
    glUniform2f(uP1_, cx + r, cy - r);
    glUniform2f(uP2_, cx + r, cy + r);
    glUniform2f(uP3_, cx - r, cy + r);
    glUniform4f(uUv_, 0, 0, 1, 1);
    glUniform4f(uColor_, color.r, color.g, color.b, color.a);
    glUniform1f(uCircle_, 1.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glUniform1f(uCircle_, 0.0f);
}

float Renderer::uiTextWidth(const std::string& text, float pixelScale) const {
    if (text.empty()) return 0.0f;
    return (static_cast<float>(text.size()) * 6.0f - 1.0f) * pixelScale;
}

void Renderer::uiText(const std::string& text, float x, float y, float pixelScale,
                      Color color) {
    uiTex(fontTex_);
    float cx = x;
    for (char raw : text) {
        char ch = raw;
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        const int code = static_cast<unsigned char>(ch);
        const int col = (code % FONT_COLS);
        const int row = (code / FONT_COLS);
        const float u0 = static_cast<float>(col * FONT_CELL) / FONT_PX_W;
        const float u1 = static_cast<float>(col * FONT_CELL + FONT_CELL) / FONT_PX_W;
        const float vTop = static_cast<float>(row * FONT_CELL + FONT_CELL) / FONT_PX_H;
        const float vBottom = static_cast<float>(row * FONT_CELL) / FONT_PX_H;
        uiQuadUv(cx, y, FONT_CELL * pixelScale, FONT_CELL * pixelScale, u0, vTop, u1,
                 vBottom, color);
        cx += 6.0f * pixelScale;
    }
}

void Renderer::uiTextCentered(const std::string& text, float y, float pixelScale,
                              Color color) {
    const float w = uiTextWidth(text, pixelScale);
    uiText(text, (static_cast<float>(width_) - w) * 0.5f, y, pixelScale, color);
}

void Renderer::uiTooltip(const std::string& text, float x, float y, float pixelScale,
                         Color color) {
    const float pad = 6.0f * uiScale_;
    const float w = uiTextWidth(text, pixelScale) + pad * 2.0f;
    const float h = 8.0f * pixelScale + pad * 2.0f;
    uiTex(whiteTex_);
    uiQuad(x - pad, y - pad, w, h, Color::rgb(0x000000, 0.35f));
    uiText(text, x, y, pixelScale, color);
}

void Renderer::drawHud(Game& game, const HudState& hud, float time) {
    UiLayout layout;
    layout.compute(width_, height_, game.flying());
    beginUi();

    const float s = layout.scale;
    const Color white = Color::rgb(0xffffff);
    const Color shadow = Color::rgb(0x000000, 0.55f);

    if (hud.underwater) {
        uiTex(whiteTex_);
        uiQuad(0, 0, static_cast<float>(width_), static_cast<float>(height_),
               Color::rgb(0x164a8a, 0.35f));
    }

    // --- crosshair
    const float chx = width_ * 0.5f, chy = height_ * 0.5f;
    uiTex(whiteTex_);
    uiQuad(chx - 11 * s, chy - 1.5f * s, 22 * s, 3 * s, Color::rgb(0xffffff, 0.85f));
    uiQuad(chx - 1.5f * s, chy - 11 * s, 3 * s, 22 * s, Color::rgb(0xffffff, 0.85f));

    // --- hotbar
    for (int i = 0; i < 9; ++i) {
        const UiLayout::Hit& slot = layout.hotbar[i];
        const bool selected = i == hud.selectedSlot;
        uiTex(whiteTex_);
        uiQuad(slot.x - 2 * s, slot.y - 2 * s, slot.w + 4 * s, slot.h + 4 * s,
               selected ? Color::rgb(0xfff0a0, 0.95f) : Color::rgb(0x101010, 0.55f));
        uiQuad(slot.x, slot.y, slot.w, slot.h, Color::rgb(0x2b2b2b, 0.85f));

        const uint8_t block = game.hotbarBlock(i);
        if (block != BLOCK_AIR) {
            const uint8_t tile = kBlocks[block].top;
            const float col = static_cast<float>(tile % ATLAS_GRID);
            const float row = static_cast<float>(tile / ATLAS_GRID);
            const float u0 = (col + 0.06f) / ATLAS_GRID;
            const float u1 = (col + 0.94f) / ATLAS_GRID;
            const float vTop = (row + 0.94f) / ATLAS_GRID;
            const float vBottom = (row + 0.06f) / ATLAS_GRID;
            uiTex(atlasTex_);
            uiQuadUv(slot.x + 3 * s, slot.y + 3 * s, slot.w - 6 * s, slot.h - 6 * s, u0,
                     vTop, u1, vBottom, white);
        }
        uiText(std::to_string(i + 1), slot.x + 3 * s, slot.y + 1 * s, std::max(1.0f, s),
               Color::rgb(0xffffff, 0.8f));
    }

    // --- joystick
    uiTex(whiteTex_);
    uiCircle(layout.joyCx, layout.joyCy, layout.joyR, Color::rgb(0xffffff, 0.10f));
    uiCircle(layout.joyCx, layout.joyCy, layout.joyR * 0.92f,
             Color::rgb(0x000000, 0.10f));
    if (hud.joystickActive) {
        uiCircle(layout.joyCx + hud.joystickX * layout.joyR * 0.55f,
                 layout.joyCy + hud.joystickY * layout.joyR * 0.55f, layout.joyR * 0.38f,
                 Color::rgb(0xffffff, 0.55f));
    } else {
        uiCircle(layout.joyCx, layout.joyCy, layout.joyR * 0.34f,
                 Color::rgb(0xffffff, 0.28f));
    }

    // --- action buttons
    auto button = [&](const UiLayout::Hit& hit, const std::string& label, Color tint,
                      bool active) {
        const float cx = hit.x + hit.w * 0.5f;
        const float cy = hit.y + hit.h * 0.5f;
        const float r = hit.w * 0.5f;
        uiTex(whiteTex_);
        uiCircle(cx, cy, r, Color::rgb(0x000000, 0.28f));
        uiCircle(cx, cy, r * 0.9f, active ? tint : tint.withAlpha(0.35f));
        const float tScale = std::max(1.0f, std::round(1.6f * s));
        const float tw = uiTextWidth(label, tScale);
        uiText(label, cx - tw * 0.5f, cy - 4.0f * tScale, tScale,
               active ? Color::rgb(0x101010) : Color::rgb(0xffffff, 0.9f));
    };
    button(layout.mine, "MINE", Color::rgb(0xe8b04a), hud.mining);
    button(layout.place, "BUILD", Color::rgb(0x7fd44a), hud.placing);
    button(layout.jump, "JUMP", Color::rgb(0xffffff), false);
    button(layout.fly, game.flying() ? "LAND" : "FLY",
           Color::rgb(0x84c8ff), game.flying());
    if (game.flying()) {
        button(layout.flyUp, "^", Color::rgb(0x84c8ff), false);
        button(layout.flyDown, "v", Color::rgb(0x84c8ff), false);
    }

    // --- top left status
    char line[160];
    std::snprintf(line, sizeof(line), "MINECRAFTC++  FPS %d", static_cast<int>(hud.fps + 0.5f));
    uiTooltip(line, 14 * s, 12 * s, std::max(1.0f, std::round(1.8f * s)), white);
    std::snprintf(line, sizeof(line), "XYZ %.1f %.1f %.1f", game.player().pos.x,
                  game.player().pos.y, game.player().pos.z);
    uiTooltip(line, 14 * s, 34 * s, std::max(1.0f, std::round(1.6f * s)),
              Color::rgb(0xe0f0ff));
    std::snprintf(line, sizeof(line), "CHUNKS %d/%d  FACES %zu  TIME %d:%02d",
                  readyChunks_, requiredChunks_, drawnFaces_,
                  static_cast<int>(time) / 60, static_cast<int>(time) % 60);
    uiTooltip(line, 14 * s, 54 * s, std::max(1.0f, std::round(1.4f * s)),
              Color::rgb(0xd8e8f8, 0.85f));

    // --- selected block name above the hotbar
    const uint8_t selectedBlock = game.hotbarBlock(hud.selectedSlot);
    if (selectedBlock != BLOCK_AIR) {
        const float tScale = std::max(1.0f, std::round(1.6f * s));
        const std::string name = kBlocks[selectedBlock].name;
        const float tw = uiTextWidth(name, tScale);
        uiTex(whiteTex_);
        uiQuad((width_ - tw) * 0.5f - 6 * s, layout.hotbar[0].y - 30 * s,
               tw + 12 * s, 16 * s, Color::rgb(0x000000, 0.35f));
        uiText(name, (width_ - tw) * 0.5f, layout.hotbar[0].y - 27 * s, tScale, white);
    }

    // --- flying / toast
    if (!hud.toast.empty() && hud.toastTimer > 0.0f) {
        const float tScale = std::max(1.0f, std::round(1.8f * s));
        const float alpha = clampf(hud.toastTimer, 0.0f, 1.0f);
        uiTextCentered(hud.toast, height_ * 0.62f, tScale,
                       Color::rgb(0xfff4c0, alpha));
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glUseProgram(0);
    (void)shadow;
}

void Renderer::drawSplash(const HudState& hud, float time) {
    beginUi();
    const float w = static_cast<float>(width_);
    const float h = static_cast<float>(height_);
    const float s = uiScale_;

    // Background.
    uiTex(whiteTex_);
    uiQuad(0, 0, w, h, Color::rgb(0x0b1220));
    const int bands = 12;
    for (int i = 0; i < bands; ++i) {
        const float t = static_cast<float>(i) / bands;
        const Color c{lerpf(0.10f, 0.02f, t), lerpf(0.16f, 0.05f, t),
                      lerpf(0.28f, 0.10f, t), 1.0f};
        uiQuad(0, h * t, w, h / bands + 1, c);
    }

    // Isometric block logo.
    const float cx = w * 0.5f;
    const float cy = h * 0.30f + std::sin(time * 1.6f) * 6.0f * s;
    const float half = 92.0f * s;
    const float topH = 52.0f * s;
    const float side = 92.0f * s;
    const float top[4][2] = {{cx, cy - topH}, {cx + half, cy}, {cx, cy + topH},
                             {cx - half, cy}};
    const float left[4][2] = {{cx - half, cy}, {cx, cy + topH}, {cx, cy + topH + side},
                              {cx - half, cy + side}};
    const float right[4][2] = {{cx, cy + topH}, {cx + half, cy}, {cx + half, cy + side},
                               {cx, cy + topH + side}};
    uiCorners(left, Color::rgb(0x7b5333), false);
    uiCorners(right, Color::rgb(0x63422a), false);
    uiCorners(top, Color::rgb(0x67a83f), false);
    const float lipL[4][2] = {{cx - half, cy}, {cx, cy + topH}, {cx, cy + topH + 13 * s},
                              {cx - half, cy + 13 * s}};
    const float lipR[4][2] = {{cx, cy + topH}, {cx + half, cy}, {cx + half, cy + 13 * s},
                              {cx, cy + topH + 13 * s}};
    uiCorners(lipL, Color::rgb(0x6fa845), false);
    uiCorners(lipR, Color::rgb(0x5e8f3b), false);

    const float titleScale = std::max(2.0f, std::round(h / 110.0f));
    uiTextCentered("MINECRAFTC++", h * 0.47f, titleScale, Color::rgb(0xffffff));
    const float subScale = std::max(1.0f, std::round(titleScale * 0.45f));
    uiTextCentered("C++ VOXEL ENGINE - ARM64", h * 0.47f + 9.0f * titleScale, subScale,
                   Color::rgb(0xbfe0ff));

    // Progress bar.
    const float barW = w * 0.5f;
    const float barH = 14.0f * s;
    const float barX = (w - barW) * 0.5f;
    const float barY = h * 0.63f;
    uiTex(whiteTex_);
    uiQuad(barX - 3 * s, barY - 3 * s, barW + 6 * s, barH + 6 * s,
           Color::rgb(0xffffff, 0.20f));
    uiQuad(barX, barY, barW, barH, Color::rgb(0x06080c, 0.8f));
    const float progress = clampf(hud.splashProgress, 0.0f, 1.0f);
    uiQuad(barX, barY, barW * progress, barH, Color::rgb(0x7fd44a));
    char pct[32];
    std::snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(progress * 100.0f));
    uiText(pct, barX + barW + 10 * s, barY, std::max(1.0f, std::round(1.4f * s)),
           Color::rgb(0xffffff, 0.9f));

    const float statusScale = std::max(1.0f, std::round(1.6f * s));
    uiTextCentered(hud.splashStatus.empty() ? "PREPARING ENGINE" : hud.splashStatus,
                   barY + barH + 14 * s, statusScale, Color::rgb(0xe8f4ff));
    uiTextCentered("CLEAN ROOM C++ BUILD - NOT AFFILIATED WITH MOJANG", h - 42 * s,
                   std::max(1.0f, std::round(1.2f * s)), Color::rgb(0xffffff, 0.55f));
    uiTextCentered("VERSION 0.1.0", h - 24 * s, std::max(1.0f, std::round(1.2f * s)),
                   Color::rgb(0xffffff, 0.35f));

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glUseProgram(0);
}

}  // namespace mcc
