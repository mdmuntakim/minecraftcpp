#include "mcc/player.hpp"

#include <algorithm>
#include <cmath>

#include "mcc/block.hpp"

namespace mcc {
namespace {
constexpr float kGravity = 30.0f;
constexpr float kJumpSpeed = 8.6f;
constexpr float kWalkSpeed = 4.4f;
constexpr float kSprintSpeed = 6.4f;
constexpr float kFlySpeed = 13.0f;
constexpr float kMaxStep = 0.05f;
constexpr float kEpsilon = 1e-4f;
}  // namespace

AABB Player::box() const {
    const float hw = kWidth * 0.5f;
    return AABB{Vec3{pos.x - hw, pos.y, pos.z - hw},
                Vec3{pos.x + hw, pos.y + kHeight, pos.z + hw}};
}

Vec3 Player::eye() const { return Vec3{pos.x, pos.y + kEye, pos.z}; }

Vec3 Player::forward() const {
    const float cp = std::cos(pitch);
    return Vec3{std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp};
}

Vec3 Player::right() const { return Vec3{std::cos(yaw), 0.0f, std::sin(yaw)}; }

bool Player::collides(const World& world, const Vec3& p) const {
    const float hw = kWidth * 0.5f - kEpsilon;
    const float x0 = p.x - hw, x1 = p.x + hw;
    const float y0 = p.y, y1 = p.y + kHeight - kEpsilon;
    const float z0 = p.z - hw, z1 = p.z + hw;

    const int bx0 = static_cast<int>(std::floor(x0));
    const int bx1 = static_cast<int>(std::floor(x1));
    const int by0 = static_cast<int>(std::floor(y0));
    const int by1 = static_cast<int>(std::floor(y1));
    const int bz0 = static_cast<int>(std::floor(z0));
    const int bz1 = static_cast<int>(std::floor(z1));

    for (int y = by0; y <= by1; ++y) {
        for (int z = bz0; z <= bz1; ++z) {
            for (int x = bx0; x <= bx1; ++x) {
                if (!isSolid(world.getBlock(x, y, z))) continue;
                if (x1 <= x || x0 >= x + 1) continue;
                if (y1 <= y || y0 >= y + 1) continue;
                if (z1 <= z || z0 >= z + 1) continue;
                return true;
            }
        }
    }
    return false;
}

void Player::moveAxis(World& world, int axis, float amount) {
    if (amount == 0.0f) return;
    const int steps = std::max(1, static_cast<int>(std::ceil(std::fabs(amount) / kMaxStep)));
    const float step = amount / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        Vec3 next = pos;
        if (axis == 0) next.x += step;
        else if (axis == 1) next.y += step;
        else next.z += step;

        if (!collides(world, next)) {
            pos = next;
            continue;
        }
        // Slide as far into the block as possible, then stop this axis.
        if (axis == 0) vel.x = 0.0f;
        else if (axis == 1) vel.y = 0.0f;
        else vel.z = 0.0f;
        return;
    }
}

void Player::update(World& world, const InputState& input, float dt) {
    dt = clampf(dt, 0.0f, 0.1f);
    if (dt <= 0.0f) return;

    yaw += input.lookDX;
    pitch = clampf(pitch + input.lookDY, -1.5533f, 1.5533f);
    // Keep yaw in a small range so the float never loses precision.
    if (yaw > 6.2831853f) yaw -= 6.2831853f;
    if (yaw < -6.2831853f) yaw += 6.2831853f;

    flying = input.flying;
    const uint8_t feetBlock = world.getBlock(static_cast<int>(std::floor(pos.x)),
                                             static_cast<int>(std::floor(pos.y + 0.1f)),
                                             static_cast<int>(std::floor(pos.z)));
    const uint8_t headBlock = world.getBlock(static_cast<int>(std::floor(pos.x)),
                                             static_cast<int>(std::floor(pos.y + kEye)),
                                             static_cast<int>(std::floor(pos.z)));
    inWater = isLiquid(feetBlock) || isLiquid(headBlock);

    float mx = input.moveX;
    float mz = input.moveZ;
    const float len = std::sqrt(mx * mx + mz * mz);
    if (len > 1.0f) {
        mx /= len;
        mz /= len;
    }

    const Vec3 fwd{std::sin(yaw), 0.0f, -std::cos(yaw)};
    const Vec3 rgt{std::cos(yaw), 0.0f, std::sin(yaw)};
    Vec3 wish = rgt * mx + fwd * mz;

    float speed;
    if (flying) speed = kFlySpeed;
    else if (inWater) speed = 2.9f;
    else speed = input.sprint ? kSprintSpeed : kWalkSpeed;

    const float accel = flying ? 12.0f : (onGround ? 16.0f : 6.0f);
    const float blend = clampf(dt * accel, 0.0f, 1.0f);
    vel.x = lerpf(vel.x, wish.x * speed, blend);
    vel.z = lerpf(vel.z, wish.z * speed, blend);

    if (flying) {
        float target = 0.0f;
        if (input.jump) target += speed;
        if (input.sneak) target -= speed;
        vel.y = lerpf(vel.y, target, clampf(dt * 14.0f, 0.0f, 1.0f));
    } else if (inWater) {
        vel.y -= kGravity * 0.25f * dt;
        if (input.jump) vel.y = 3.6f;
        vel.y = clampf(vel.y, -4.0f, 4.0f);
    } else {
        vel.y -= kGravity * dt;
        vel.y = std::max(vel.y, -64.0f);
        if (input.jump && onGround) {
            vel.y = kJumpSpeed;
            onGround = false;
        }
    }

    // Horizontal movement, then vertical. Axis separation is what makes a
    // wall stop the player instead of trapping them.
    moveAxis(world, 0, vel.x * dt);
    moveAxis(world, 2, vel.z * dt);
    moveAxis(world, 1, vel.y * dt);

    if (flying) {
        onGround = false;
    } else {
        onGround = collides(world, Vec3{pos.x, pos.y - 0.02f, pos.z});
    }

    if (pos.y < -8.0f) {
        respawn(world, static_cast<int>(std::floor(pos.x)),
                static_cast<int>(std::floor(pos.z)));
    }
}

void Player::respawn(World& world, int x, int z) {
    int y = WORLD_H - 2;
    while (y > 1 && !isSolid(world.getBlock(x, y, z))) --y;
    pos = Vec3{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 1.05f,
               static_cast<float>(z) + 0.5f};
    vel = Vec3{};
    onGround = false;
}

RayHit Player::raycast(const World& world, float maxDistance) const {
    RayHit hit;
    const Vec3 origin = eye();
    const Vec3 dir = forward();

    int x = static_cast<int>(std::floor(origin.x));
    int y = static_cast<int>(std::floor(origin.y));
    int z = static_cast<int>(std::floor(origin.z));

    const int stepX = dir.x > 0 ? 1 : (dir.x < 0 ? -1 : 0);
    const int stepY = dir.y > 0 ? 1 : (dir.y < 0 ? -1 : 0);
    const int stepZ = dir.z > 0 ? 1 : (dir.z < 0 ? -1 : 0);

    const float invX = dir.x != 0.0f ? std::fabs(1.0f / dir.x) : 1e30f;
    const float invY = dir.y != 0.0f ? std::fabs(1.0f / dir.y) : 1e30f;
    const float invZ = dir.z != 0.0f ? std::fabs(1.0f / dir.z) : 1e30f;

    auto firstBoundary = [](float o, int s, float inv) {
        if (s == 0) return 1e30f;
        const float cell = s > 0 ? std::floor(o) + 1.0f : std::floor(o);
        return std::fabs(cell - o) * inv;
    };

    float tMaxX = firstBoundary(origin.x, stepX, invX);
    float tMaxY = firstBoundary(origin.y, stepY, invY);
    float tMaxZ = firstBoundary(origin.z, stepZ, invZ);
    const float tDeltaX = invX, tDeltaY = invY, tDeltaZ = invZ;

    int faceX = 0, faceY = 0, faceZ = 0;
    float t = 0.0f;
    for (int i = 0; i < 256 && t <= maxDistance; ++i) {
        const uint8_t id = world.getBlock(x, y, z);
        if (isSolid(id)) {
            hit.hit = true;
            hit.x = x;
            hit.y = y;
            hit.z = z;
            hit.block = id;
            hit.nx = faceX;
            hit.ny = faceY;
            hit.nz = faceZ;
            hit.distance = t;
            return hit;
        }
        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            x += stepX;
            t = tMaxX;
            tMaxX += tDeltaX;
            faceX = -stepX; faceY = 0; faceZ = 0;
        } else if (tMaxY < tMaxZ) {
            y += stepY;
            t = tMaxY;
            tMaxY += tDeltaY;
            faceX = 0; faceY = -stepY; faceZ = 0;
        } else {
            z += stepZ;
            t = tMaxZ;
            tMaxZ += tDeltaZ;
            faceX = 0; faceY = 0; faceZ = -stepZ;
        }
    }
    return hit;
}

bool Player::placementCell(const RayHit& hit, int& ox, int& oy, int& oz) const {
    if (!hit.hit) return false;
    ox = hit.x + hit.nx;
    oy = hit.y + hit.ny;
    oz = hit.z + hit.nz;
    return true;
}

}  // namespace mcc
