#pragma once

#include <vector>
#include <Entities/Character.h>
#include <Entities/Character.h>
#include <Entities/Enemy.h>
#include <Components/PhysicsObstacle.h>

class StealthManager {
public:
    StealthManager() = default;
    ~StealthManager() = default;

    // Evaluates detection for all enemies based on the player
    void update(const std::vector<Character*>& characters, Character* player, const std::vector<PhysicsObstacle>& obstacles, float dt);

    void drawDebug(const std::vector<Character*>& characters) const;
};
