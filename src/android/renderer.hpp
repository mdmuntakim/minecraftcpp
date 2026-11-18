// Android renderer: GLES 3.0 terrain, sky, HUD and splash screen.
#pragma once

#include <GLES3/gl3.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "mcc/game.hpp"
#include "mcc/math.hpp"
#include "mcc/mesher.hpp"

namespace mcc {

struct UiLayout {
    struct Hit {
        float x = 0, y = 0, w = 0, h = 0;
        bool contains(float px, float py) const {
            return px >= x && py >= y && px <= x + w && py <= y + h;
        }
    };

    int width = 0;
    int height = 0;
    float scale = 1.0f;
    float joyCx = 0, joyCy = 0, joyR = 0;
    Hit jump, mine, place, fly, flyUp, flyDown;
    Hit hotbar[9];

    void compute(int w, int h, bool flying);
};

struct HudState {
    bool splash = true;
    bool playing = false;
    float splashProgress = 0.0f;
    std::string splashStatus;
    float fps = 0.0f;
    bool joystickActive = false;
    float joystickX = 0.0f, joystickY = 0.0f;  // normalised -1..1
    bool flying = false;
    bool mining = false;
    bool placing = false;
    bool underwater = false;
    float toastTimer = 0.0f;
    std::string toast;
    int selectedSlot = 0;
};

class Renderer {
public:
    bool init(int width, int height, std::string* error);
    void shutdown();
    void resize(int width, int height);

    // Generates and meshes the chunks around the player. maxMilliseconds <= 0
    // means "no budget, finish the whole radius".
    void updateWorld(Game& game, int radius, float maxMilliseconds);

    void draw(Game& game, const HudState& hud, float time);

    int readyChunks() const { return readyChunks_; }
    int requiredChunks() const { return requiredChunks_; }
    size_t drawnFaces() const { return drawnFaces_; }

    struct GpuMesh {
        GLuint vao = 0, vbo = 0, ibo = 0;
        GLsizei count = 0;
        void release();
    };

private:
    struct GpuChunk {
        GpuMesh opaque;
        GpuMesh water;
        uint32_t revision = 0;
        uint32_t seam = 0;
        float dist2 = 0.0f;
        bool visible = false;
    };

    bool buildUi();
    void buildChunk(Game& game, int cx, int cz, const Chunk& chunk);
    void releaseChunk(GpuChunk& g);
    void drawSky(const Mat4& vp, const Vec3& eye, float time);
    void drawTerrain(Game& game, int radius, float time);
    void drawHud(Game& game, const HudState& hud, float time);
    void drawSplash(const HudState& hud, float time);

    void beginUi();
    void uiTex(GLuint tex);
    void uiQuad(float x, float y, float w, float h, Color color);
    void uiQuadUv(float x, float y, float w, float h, float u0, float v0, float u1,
                  float v1, Color color);
    void uiCorners(const float pts[4][2], Color color, bool circle);
    void uiCircle(float cx, float cy, float r, Color color);
    void uiText(const std::string& text, float x, float y, float pixelScale, Color color);
    float uiTextWidth(const std::string& text, float pixelScale) const;
    void uiTextCentered(const std::string& text, float y, float pixelScale, Color color);
    void uiTooltip(const std::string& text, float x, float y, float pixelScale,
                   Color color);

    int width_ = 0, height_ = 0;
    float uiScale_ = 1.0f;

    GLuint progTerrain_ = 0, progUi_ = 0;
    GLuint whiteTex_ = 0, atlasTex_ = 0, fontTex_ = 0;
    GLuint uiVao_ = 0, uiVbo_ = 0;

    // terrain uniforms
    GLint tVP_ = -1, tOffset_ = -1, tCamPos_ = -1, tAtlas_ = -1, tFogColor_ = -1;
    GLint tFogStart_ = -1, tFogEnd_ = -1, tAlphaTest_ = -1, tTint_ = -1;
    // ui uniforms
    GLint uScreen_ = -1, uP0_ = -1, uP1_ = -1, uP2_ = -1, uP3_ = -1;
    GLint uUv_ = -1, uColor_ = -1, uCircle_ = -1, uTex_ = -1;

    std::unordered_map<uint64_t, GpuChunk> chunks_;
    MeshData staging_;
    int readyChunks_ = 0;
    int requiredChunks_ = 0;
    size_t drawnFaces_ = 0;
    uint64_t frame_ = 0;
    bool uiReady_ = false;
};

}  // namespace mcc
