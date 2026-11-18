// Player physics: AABB collision against the voxel grid, walking, sprinting,
// jumping, swimming and creative flight, plus a DDA block raycast for the
// crosshair.
#pragma once

#include "mcc/math.hpp"
#include "mcc/world.hpp"

namespace mcc {

struct InputState {
    float moveX = 0.0f;   // -1 left .. +1 right
    float moveZ = 0.0f;   // -1 back .. +1 forward
    float lookDX = 0.0f;  // radians
    float lookDY = 0.0f;
    bool jump = false;
    bool sneak = false;
    bool sprint = false;
    bool flying = false;
};

struct RayHit {
    bool hit = false;
    int x = 0, y = 0, z = 0;
    int nx = 0, ny = 0, nz = 0;
    uint8_t block = 0;
    float distance = 0.0f;
};

class Player {
public:
    static constexpr float kWidth = 0.6f;
    static constexpr float kHeight = 1.8f;
    static constexpr float kEye = 1.62f;

    Vec3 pos{0.5f, 80.0f, 0.5f};
    Vec3 vel{};
    float yaw = 0.0f;    // 0 looks towards -Z
    float pitch = 0.0f;  // radians, positive looks up
    bool onGround = false;
    bool inWater = false;
    bool flying = false;

    AABB box() const;
    Vec3 eye() const;
    Vec3 forward() const;
    Vec3 right() const;

    void update(World& world, const InputState& input, float dt);
    void respawn(World& world, int x, int z);

    RayHit raycast(const World& world, float maxDistance) const;

    // The block the player is allowed to place into, one step before the hit.
    bool placementCell(const RayHit& hit, int& ox, int& oy, int& oz) const;

private:
    bool collides(const World& world, const Vec3& p) const;
    void moveAxis(World& world, int axis, float amount);
};

}  // namespace mcc
