#pragma once

#include <vector>
#include <Entities/Character.h>
#include <Entities/Enemy.h>

class StealthManager {
public:
    StealthManager() = default;
    ~StealthManager() = default;

    // Evaluates detection for all enemies based on the player's position
    void update(const std::vector<Character*>& characters, const Vector3& player_pos);
};
