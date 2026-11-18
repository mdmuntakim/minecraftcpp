// Game rules: hotbar, block interaction and the spawn / streaming logic.
#pragma once

#include <cstdint>
#include <vector>

#include "mcc/player.hpp"
#include "mcc/world.hpp"

namespace mcc {

struct BlockEdit {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
    uint8_t id = 0;
};

class Game {
public:
    explicit Game(uint32_t seed = 20240601U);

    void reset(uint32_t seed);
    void update(float dt, const InputState& input);

    // Places the player on the highest land near the origin.
    void prepareSpawn();

    bool breakBlock();
    bool placeBlock();

    void selectSlot(int slot);
    void cycleSlot(int delta);

    // Every change the player makes, kept so the Android build can persist
    // the world without saving the whole terrain.
    const std::vector<BlockEdit>& edits() const { return edits_; }
    void applyEdit(int x, int y, int z, uint8_t id);

    World& world() { return world_; }
    const World& world() const { return world_; }
    Player& player() { return player_; }
    const Player& player() const { return player_; }

    int selectedSlot() const { return selected_; }
    uint8_t hotbarBlock(int slot) const {
        return (slot >= 0 && slot < 9) ? hotbar_[slot] : static_cast<uint8_t>(BLOCK_AIR);
    }

    // Generates the missing chunks in a square around the player; returns how
    // many chunks are still missing afterwards.
    int streamChunks(int radius);

    bool toggleFlying() {
        flying_ = !flying_;
        return flying_;
    }
    bool flying() const { return flying_; }

    RayHit target() const { return player_.raycast(world_, 6.0f); }

private:
    World world_;
    Player player_;
    uint8_t hotbar_[9];
    int selected_ = 0;
    bool flying_ = false;
    std::vector<BlockEdit> edits_;
};

}  // namespace mcc
