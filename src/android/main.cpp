// Android entry point: NativeActivity + EGL + GLES 3.0, no Java, no engine.
// Everything below is the game loop, the touch controls and the save file.
#include <android/log.h>
#include <android/native_window.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "mcc/block.hpp"
#include "mcc/game.hpp"
#include "renderer.hpp"

#define LOG_TAG "MinecraftCpp"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

using mcc::Color;
using mcc::Game;
using mcc::HudState;
using mcc::InputState;
using mcc::Renderer;
using mcc::UiLayout;
using mcc::Vec3;

constexpr int kViewRadius = 5;
constexpr float kSplashMinSeconds = 1.1f;
constexpr double kSaveInterval = 30.0;

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

enum class Role { None, Joystick, Look, Button };

struct Pointer {
    int32_t id = -1;
    Role role = Role::None;
    float x = 0, y = 0;
    float lastX = 0, lastY = 0;
    float downX = 0, downY = 0;
    float travel = 0;
    double downTime = 0;
    std::string button;
};

struct SaveData {
    bool valid = false;
    uint32_t seed = 0;
    float px = 0, py = 80, pz = 0;
    float yaw = 0, pitch = 0;
    int slot = 0;
    bool flying = false;
    std::vector<mcc::BlockEdit> edits;
};

struct App {
    android_app* android = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    EGLConfig config = nullptr;

    int width = 0;
    int height = 0;
    bool haveSurface = false;
    bool rendererReady = false;
    bool focused = false;
    double startTime = 0;
    double lastFrame = 0;
    double lastSave = 0;

    Game game{20240601U};
    Renderer renderer;
    UiLayout layout;
    std::vector<Pointer> pointers;
    InputState touchInput;
    HudState hud;
    float fps = 60.0f;
    float mineCooldown = 0.0f;
    bool mineHeld = false;
    bool placeHeld = false;
    bool jumpHeld = false;
    bool sneakHeld = false;
    double lastJumpTap = 0;
    float toastTimer = 0.0f;
    std::string toast;
    bool splash = true;
    bool keys[256] = {false};

    std::string savePath() const {
        if (!android || !android->activity || !android->activity->internalDataPath) return {};
        return std::string(android->activity->internalDataPath) + "/world.dat";
    }
};

// --------------------------------------------------------------------- save
bool writeSave(const App& app) {
    const std::string path = app.savePath();
    if (path.empty()) return false;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    const char magic[4] = {'M', 'C', 'C', 'W'};
    const uint32_t version = 1;
    const uint32_t seed = app.game.world().seed();
    const mcc::Player& p = app.game.player();
    uint8_t slot = static_cast<uint8_t>(app.game.selectedSlot());
    uint8_t flying = app.game.flying() ? 1 : 0;
    const auto& edits = app.game.edits();
    const uint32_t count = static_cast<uint32_t>(edits.size());

    std::fwrite(magic, 1, 4, f);
    std::fwrite(&version, sizeof(version), 1, f);
    std::fwrite(&seed, sizeof(seed), 1, f);
    std::fwrite(&p.pos.x, sizeof(float), 1, f);
    std::fwrite(&p.pos.y, sizeof(float), 1, f);
    std::fwrite(&p.pos.z, sizeof(float), 1, f);
    std::fwrite(&p.yaw, sizeof(float), 1, f);
    std::fwrite(&p.pitch, sizeof(float), 1, f);
    std::fwrite(&slot, 1, 1, f);
    std::fwrite(&flying, 1, 1, f);
    std::fwrite(&count, sizeof(count), 1, f);
    if (count) {
        std::fwrite(edits.data(), sizeof(mcc::BlockEdit), count, f);
    }
    std::fclose(f);
    return true;
}

SaveData readSave(const std::string& path) {
    SaveData save;
    if (path.empty()) return save;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return save;
    char magic[4] = {0};
    uint32_t version = 0, seed = 0;
    if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "MCCW", 4) != 0 ||
        std::fread(&version, sizeof(version), 1, f) != 1 || version != 1 ||
        std::fread(&seed, sizeof(seed), 1, f) != 1) {
        std::fclose(f);
        return save;
    }
    uint8_t slot = 0, flying = 0;
    uint32_t count = 0;
    std::fread(&save.px, sizeof(float), 1, f);
    std::fread(&save.py, sizeof(float), 1, f);
    std::fread(&save.pz, sizeof(float), 1, f);
    std::fread(&save.yaw, sizeof(float), 1, f);
    std::fread(&save.pitch, sizeof(float), 1, f);
    std::fread(&slot, 1, 1, f);
    std::fread(&flying, 1, 1, f);
    std::fread(&count, sizeof(count), 1, f);
    if (count > 500000U) {
        std::fclose(f);
        return save;
    }
    save.edits.resize(count);
    if (count) std::fread(save.edits.data(), sizeof(mcc::BlockEdit), count, f);
    std::fclose(f);
    save.seed = seed;
    save.slot = slot;
    save.flying = flying != 0;
    save.valid = true;
    return save;
}

// ---------------------------------------------------------------------- EGL
void destroyEgl(App& app) {
    if (app.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(app.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (app.context != EGL_NO_CONTEXT) eglDestroyContext(app.display, app.context);
        if (app.surface != EGL_NO_SURFACE) eglDestroySurface(app.display, app.surface);
        eglTerminate(app.display);
    }
    app.display = EGL_NO_DISPLAY;
    app.surface = EGL_NO_SURFACE;
    app.context = EGL_NO_CONTEXT;
    app.config = nullptr;
    app.haveSurface = false;
    app.rendererReady = false;
}

bool initEgl(App& app) {
    app.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (app.display == EGL_NO_DISPLAY) {
        LOGE("no EGL display");
        return false;
    }
    if (!eglInitialize(app.display, nullptr, nullptr)) {
        LOGE("eglInitialize failed");
        return false;
    }

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_DEPTH_SIZE,      16,
        EGL_NONE,
    };
    EGLint numConfigs = 0;
    if (!eglChooseConfig(app.display, attribs, &app.config, 1, &numConfigs) ||
        numConfigs < 1) {
        LOGE("no OpenGL ES 3.0 config");
        return false;
    }

    EGLint format = 0;
    eglGetConfigAttrib(app.display, app.config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(app.android->window, 0, 0, format);

    app.surface = eglCreateWindowSurface(app.display, app.config, app.android->window,
                                         nullptr);
    if (app.surface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed");
        return false;
    }

    const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    app.context = eglCreateContext(app.display, app.config, EGL_NO_CONTEXT, ctxAttribs);
    if (app.context == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext(ES3) failed");
        return false;
    }
    if (!eglMakeCurrent(app.display, app.surface, app.surface, app.context)) {
        LOGE("eglMakeCurrent failed");
        return false;
    }
    eglQuerySurface(app.display, app.surface, EGL_WIDTH, &app.width);
    eglQuerySurface(app.display, app.surface, EGL_HEIGHT, &app.height);
    eglSwapInterval(app.display, 1);

    LOGI("GLES %s, %dx%d", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
         app.width, app.height);
    return true;
}

std::string glString(GLenum name) {
    const GLubyte* s = glGetString(name);
    return s ? std::string(reinterpret_cast<const char*>(s)) : std::string("?");
}

bool setupRenderer(App& app) {
    if (app.rendererReady) return true;
    std::string error;
    if (!app.renderer.init(app.width, app.height, &error)) {
        LOGE("renderer init failed: %s", error.c_str());
        return false;
    }
    app.rendererReady = true;
    app.layout.compute(app.width, app.height, app.game.flying());
    LOGI("renderer ready (%s)", glString(GL_RENDERER).c_str());
    return true;
}

// -------------------------------------------------------------------- input
void resetTouchState(App& app) {
    app.pointers.clear();
    app.touchInput = InputState{};
    app.mineHeld = false;
    app.placeHeld = false;
    app.jumpHeld = false;
    app.sneakHeld = false;
}

void showToast(App& app, const std::string& text) {
    app.toast = text;
    app.toastTimer = 1.4f;
}

void toggleFlying(App& app) {
    app.game.toggleFlying();
    showToast(app, app.game.flying() ? "FLY MODE" : "WALK MODE");
}

Pointer* findPointer(App& app, int32_t id) {
    for (Pointer& p : app.pointers) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

// Returns the button name under the point, or empty.
std::string buttonAt(App& app, float x, float y) {
    app.layout.compute(app.width, app.height, app.game.flying());
    const UiLayout& l = app.layout;
    if (l.mine.contains(x, y)) return "mine";
    if (l.place.contains(x, y)) return "place";
    if (l.jump.contains(x, y)) return "jump";
    if (l.fly.contains(x, y)) return "fly";
    if (app.game.flying()) {
        if (l.flyUp.contains(x, y)) return "up";
        if (l.flyDown.contains(x, y)) return "down";
    }
    for (int i = 0; i < 9; ++i) {
        if (l.hotbar[i].contains(x, y)) return "slot" + std::to_string(i);
    }
    return {};
}

void pressButton(App& app, const std::string& name) {
    if (name == "mine") {
        app.mineHeld = true;
        if (app.game.breakBlock()) app.mineCooldown = 0.20f;
    } else if (name == "place") {
        app.placeHeld = true;
        app.game.placeBlock();
    } else if (name == "jump") {
        app.jumpHeld = true;
        const double t = nowSeconds();
        if (t - app.lastJumpTap < 0.32) {
            toggleFlying(app);
            app.lastJumpTap = 0.0;
        } else {
            app.lastJumpTap = t;
        }
    } else if (name == "fly") {
        toggleFlying(app);
    } else if (name == "up") {
        app.jumpHeld = true;
    } else if (name == "down") {
        app.sneakHeld = true;
    } else if (name.rfind("slot", 0) == 0) {
        app.game.selectSlot(std::atoi(name.c_str() + 4));
    }
}

void releaseButton(App& app, const std::string& name) {
    if (name == "mine") app.mineHeld = false;
    else if (name == "place") app.placeHeld = false;
    else if (name == "jump") app.jumpHeld = false;
    else if (name == "up") app.jumpHeld = false;
    else if (name == "down") app.sneakHeld = false;
}

void pointerDown(App& app, int32_t id, float x, float y) {
    app.layout.compute(app.width, app.height, app.game.flying());
    Pointer p;
    p.id = id;
    p.x = p.lastX = p.downX = x;
    p.y = p.lastY = p.downY = y;
    p.downTime = nowSeconds();

    const std::string button = buttonAt(app, x, y);
    if (!button.empty()) {
        p.role = Role::Button;
        p.button = button;
        pressButton(app, button);
    } else if (x < app.width * 0.45f && y > app.height * 0.35f) {
        p.role = Role::Joystick;
        app.touchInput.moveX = 0.0f;
        app.touchInput.moveZ = 0.0f;
    } else {
        p.role = Role::Look;
    }
    app.pointers.push_back(p);
}

void pointerMove(App& app, int32_t id, float x, float y) {
    Pointer* p = findPointer(app, id);
    if (!p) return;
    const float dx = x - p->lastX;
    const float dy = y - p->lastY;
    p->travel += std::fabs(dx) + std::fabs(dy);
    p->x = x;
    p->y = y;

    if (p->role == Role::Joystick) {
        app.layout.compute(app.width, app.height, app.game.flying());
        const float rx = (x - p->downX) / app.layout.joyR;
        const float ry = (y - p->downY) / app.layout.joyR;
        const float len = std::sqrt(rx * rx + ry * ry);
        float nx = rx, ny = ry;
        if (len > 1.0f) {
            nx /= len;
            ny /= len;
        }
        app.touchInput.moveX = nx;
        app.touchInput.moveZ = -ny;
        app.hud.joystickX = nx;
        app.hud.joystickY = ny;
        app.hud.joystickActive = true;
    } else if (p->role == Role::Look) {
        const float sensitivity = 2.8f / static_cast<float>(app.height);
        app.touchInput.lookDX += dx * sensitivity;
        app.touchInput.lookDY -= dy * sensitivity;
    }
    p->lastX = x;
    p->lastY = y;
}

void pointerUp(App& app, int32_t id) {
    Pointer* p = findPointer(app, id);
    if (!p) return;
    if (p->role == Role::Joystick) {
        app.touchInput.moveX = 0.0f;
        app.touchInput.moveZ = 0.0f;
        app.hud.joystickActive = false;
        app.hud.joystickX = app.hud.joystickY = 0.0f;
    }
    if (p->role == Role::Button) {
        releaseButton(app, p->button);
    }
    if (p->role == Role::Look) {
        const double held = nowSeconds() - p->downTime;
        if (p->travel < 14.0f && held < 0.30) {
            // Pocket-edition style tap: break the block under the crosshair.
            app.game.breakBlock();
        }
    }
    app.pointers.erase(std::remove_if(app.pointers.begin(), app.pointers.end(),
                                      [id](const Pointer& q) { return q.id == id; }),
                       app.pointers.end());
}

void handleKey(App& app, int32_t keyCode, bool down) {
    if (keyCode < 0 || keyCode >= 256) return;
    app.keys[keyCode] = down;
    if (!down) return;
    switch (keyCode) {
        case AKEYCODE_F:
            toggleFlying(app);
            break;
        case AKEYCODE_E:
            app.game.placeBlock();
            break;
        case AKEYCODE_Q:
            app.game.breakBlock();
            break;
        case AKEYCODE_1:
        case AKEYCODE_2:
        case AKEYCODE_3:
        case AKEYCODE_4:
        case AKEYCODE_5:
        case AKEYCODE_6:
        case AKEYCODE_7:
        case AKEYCODE_8:
        case AKEYCODE_9:
            app.game.selectSlot(keyCode - AKEYCODE_1);
            break;
        default:
            break;
    }
}

int32_t inputEvent(android_app* state, AInputEvent* event) {
    App* app = static_cast<App*>(state->userData);
    if (!app) return 0;

    const int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_KEY) {
        if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN ||
            AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP) {
            handleKey(*app, AKeyEvent_getKeyCode(event),
                      AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN);
            return 1;
        }
        return 0;
    }
    if (type != AINPUT_EVENT_TYPE_MOTION) return 0;
    if (app->splash || !app->rendererReady) return 1;

    const int32_t action = AMotionEvent_getAction(event);
    const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
    const size_t index = static_cast<size_t>(
        (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
        AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    const size_t count = AMotionEvent_getPointerCount(event);

    switch (masked) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN: {
            const int32_t id = AMotionEvent_getPointerId(event, index);
            pointerDown(*app, id, AMotionEvent_getX(event, index),
                        AMotionEvent_getY(event, index));
            break;
        }
        case AMOTION_EVENT_ACTION_MOVE: {
            for (size_t i = 0; i < count; ++i) {
                pointerMove(*app, AMotionEvent_getPointerId(event, i),
                            AMotionEvent_getX(event, i), AMotionEvent_getY(event, i));
            }
            break;
        }
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP: {
            pointerUp(*app, AMotionEvent_getPointerId(event, index));
            break;
        }
        case AMOTION_EVENT_ACTION_CANCEL:
            resetTouchState(*app);
            break;
        default:
            break;
    }
    return 1;
}

// ------------------------------------------------------------------- engine
void handleCommand(android_app* state, int32_t command) {
    App* app = static_cast<App*>(state->userData);
    if (!app) return;
    switch (command) {
        case APP_CMD_INIT_WINDOW:
            if (state->window) {
                if (!initEgl(*app) || !setupRenderer(*app)) {
                    LOGE("failed to initialise the surface");
                    destroyEgl(*app);
                    break;
                }
                app->haveSurface = true;
                app->startTime = nowSeconds();
                app->lastFrame = app->startTime;
                app->lastSave = app->startTime;
                app->hud.splash = app->splash;
            }
            break;
        case APP_CMD_TERM_WINDOW:
            writeSave(*app);
            app->renderer.shutdown();
            destroyEgl(*app);
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            if (app->display != EGL_NO_DISPLAY) {
                eglQuerySurface(app->display, app->surface, EGL_WIDTH, &app->width);
                eglQuerySurface(app->display, app->surface, EGL_HEIGHT, &app->height);
                if (app->width > 0 && app->height > 0) {
                    app->renderer.resize(app->width, app->height);
                    app->layout.compute(app->width, app->height, app->game.flying());
                }
            }
            break;
        case APP_CMD_GAINED_FOCUS:
            app->focused = true;
            break;
        case APP_CMD_LOST_FOCUS:
        case APP_CMD_PAUSE:
            app->focused = false;
            writeSave(*app);
            break;
        case APP_CMD_LOW_MEMORY:
            LOGW("low memory event");
            break;
        default:
            break;
    }
}

void buildInput(App& app, InputState& input) {
    input = app.touchInput;
    // Held touch buttons.
    if (app.jumpHeld) input.jump = true;
    if (app.sneakHeld) input.sneak = true;
    const float len = std::sqrt(input.moveX * input.moveX + input.moveZ * input.moveZ);
    if (len > 0.92f) input.sprint = true;

    // Keyboard (emulator / Chromebook / physical keyboard).
    if (app.keys[AKEYCODE_W] || app.keys[AKEYCODE_DPAD_UP]) input.moveZ += 1.0f;
    if (app.keys[AKEYCODE_S] || app.keys[AKEYCODE_DPAD_DOWN]) input.moveZ -= 1.0f;
    if (app.keys[AKEYCODE_A] || app.keys[AKEYCODE_DPAD_LEFT]) input.moveX -= 1.0f;
    if (app.keys[AKEYCODE_D] || app.keys[AKEYCODE_DPAD_RIGHT]) input.moveX += 1.0f;
    if (app.keys[AKEYCODE_SPACE]) input.jump = true;
    if (app.keys[AKEYCODE_SHIFT_LEFT] || app.keys[AKEYCODE_SHIFT_RIGHT]) input.sneak = true;
    const float lookSpeed = 0.035f;
    if (app.keys[AKEYCODE_J]) input.lookDX -= lookSpeed;
    if (app.keys[AKEYCODE_L]) input.lookDX += lookSpeed;
    if (app.keys[AKEYCODE_I]) input.lookDY += lookSpeed;
    if (app.keys[AKEYCODE_K]) input.lookDY -= lookSpeed;
    input.flying = app.game.flying();

    const float mlen = std::sqrt(input.moveX * input.moveX + input.moveZ * input.moveZ);
    if (mlen > 1.0f) {
        input.moveX /= mlen;
        input.moveZ /= mlen;
    }
}

void renderFrame(App& app) {
    const double now = nowSeconds();
    float dt = static_cast<float>(now - app.lastFrame);
    app.lastFrame = now;
    dt = mcc::clampf(dt, 0.0f, 0.1f);
    if (dt > 0.0001f) app.fps = mcc::lerpf(app.fps, 1.0f / dt, 0.08f);

    if (app.splash) {
        app.renderer.updateWorld(app.game, kViewRadius, 18.0f);
        const int ready = app.renderer.readyChunks();
        const int required = std::max(1, app.renderer.requiredChunks());
        const float progress = static_cast<float>(ready) / required;
        app.hud.splash = true;
        app.hud.splashProgress = mcc::clampf(progress * 0.9f, 0.0f, 0.95f);
        char status[96];
        std::snprintf(status, sizeof(status), "GENERATING TERRAIN %d/%d CHUNKS", ready,
                      required);
        app.hud.splashStatus = status;
        if (progress >= 1.0f && now - app.startTime > kSplashMinSeconds) {
            app.splash = false;
            app.hud.splash = false;
            app.lastFrame = now;
            app.game.streamChunks(kViewRadius + 1);
            LOGI("world ready: %d chunks", app.renderer.readyChunks());
        }
    } else {
        app.renderer.updateWorld(app.game, kViewRadius, 5.0f);

        InputState input;
        buildInput(app, input);
        app.game.update(dt, input);

        // Clear the per-frame look delta, the drag events accumulate into it.
        app.touchInput.lookDX = 0.0f;
        app.touchInput.lookDY = 0.0f;

        app.mineCooldown -= dt;
        if (app.mineHeld && app.mineCooldown <= 0.0f) {
            if (app.game.breakBlock()) app.mineCooldown = 0.18f;
            else app.mineCooldown = 0.10f;
        }
        app.toastTimer = std::max(0.0f, app.toastTimer - dt);

        app.hud.fps = app.fps;
        app.hud.playing = true;
        app.hud.selectedSlot = app.game.selectedSlot();
        app.hud.toast = app.toast;
        app.hud.toastTimer = app.toastTimer;
        app.hud.mining = app.mineHeld;
        app.hud.placing = app.placeHeld;
        app.hud.flying = app.game.flying();
        app.hud.underwater = app.game.player().inWater;

        if (now - app.lastSave > kSaveInterval) {
            writeSave(app);
            app.lastSave = now;
        }
    }

    app.hud.joystickActive = app.touchInput.moveX != 0.0f || app.touchInput.moveZ != 0.0f;
    if (app.hud.joystickActive) {
        app.hud.joystickX = app.touchInput.moveX;
        app.hud.joystickY = -app.touchInput.moveZ;
    }

    app.renderer.draw(app.game, app.hud, static_cast<float>(now - app.startTime));
    if (!eglSwapBuffers(app.display, app.surface)) {
        LOGW("eglSwapBuffers failed: 0x%x", eglGetError());
        app.renderer.shutdown();
        destroyEgl(app);
    }
}

}  // namespace

void android_main(struct android_app* state) {
    App app;
    app.android = state;
    state->userData = &app;

    // Fullscreen, no status bar, screen stays awake while playing.
    ANativeActivity_setWindowFlags(state->activity,
                                   AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_KEEP_SCREEN_ON,
                                   0);

    // Restore the saved world before the first frame so the player wakes up
    // where they left off.
    const std::string path = app.savePath();
    const SaveData save = readSave(path);
    app.game.prepareSpawn();
    if (save.valid) {
        app.game.reset(save.seed);
        for (const mcc::BlockEdit& edit : save.edits) {
            app.game.applyEdit(edit.x, edit.y, edit.z, edit.id);
        }
        app.game.player().pos = Vec3{save.px, save.py, save.pz};
        app.game.player().yaw = save.yaw;
        app.game.player().pitch = save.pitch;
        app.game.selectSlot(save.slot);
        if (save.flying != app.game.flying()) app.game.toggleFlying();

        // If the saved position is now inside a block (a different build, an
        // edited column), put the player back on the surface instead of
        // leaving them stuck.
        const int fx = static_cast<int>(std::floor(save.px));
        const int fz = static_cast<int>(std::floor(save.pz));
        const int fy = static_cast<int>(std::floor(save.py));
        const int cx = fx >= 0 ? fx / mcc::CHUNK_SX : -((-fx + mcc::CHUNK_SX - 1) / mcc::CHUNK_SX);
        const int cz = fz >= 0 ? fz / mcc::CHUNK_SZ : -((-fz + mcc::CHUNK_SZ - 1) / mcc::CHUNK_SZ);
        app.game.world().ensureChunk(cx, cz);
        if (mcc::isSolid(app.game.world().getBlock(fx, fy, fz)) ||
            mcc::isSolid(app.game.world().getBlock(fx, fy + 1, fz))) {
            app.game.player().respawn(app.game.world(), fx, fz);
            LOGW("saved position was blocked, respawned on the surface");
        }
        LOGI("loaded save: seed %u, %zu edits", save.seed, save.edits.size());
    }

    state->onAppCmd = handleCommand;
    state->onInputEvent = inputEvent;

    app.startTime = nowSeconds();
    app.lastFrame = app.startTime;
    app.lastSave = app.startTime;
    app.layout.compute(1, 1, false);

    while (true) {
        int timeout = app.haveSurface ? 0 : 8;
        while (true) {
            int events = 0;
            void* data = nullptr;
            const int ident = ALooper_pollOnce(timeout, nullptr, &events, &data);
            if (ident == ALOOPER_POLL_TIMEOUT || ident == ALOOPER_POLL_ERROR) break;
            if (data) {
                auto* source = static_cast<android_poll_source*>(data);
                source->process(state, source);
            }
            if (state->destroyRequested) break;
            timeout = 0;
        }
        if (state->destroyRequested) break;
        if (app.haveSurface && app.rendererReady) {
            renderFrame(app);
        }
    }

    writeSave(app);
    app.renderer.shutdown();
    destroyEgl(app);
    LOGI("bye");
}
