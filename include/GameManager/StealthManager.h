#pragma once

#include <vector>
#include <Entities/Character.h>
#include <Entities/Character.h>
#include <Entities/Enemy.h>
#include <Components/PhysicsObstacle.h>

#include <GameManager/SmokeCloud.h>

class StealthManager {
public:
    StealthManager() = default;
    ~StealthManager() = default;

    // Evaluates detection for all enemies based on the player
    void update(const std::vector<Character*>& characters, Character* player, const std::vector<PhysicsObstacle>& obstacles, const CollisionMesh* mesh, const std::vector<SmokeCloud>& smoke_clouds, float dt);

    // Emits a noise in world space alerting all living enemies within radius
    static void emitNoise(const Vector3& noise_pos, float radius,
                          const std::vector<Character*>& characters,
                          Character* player = nullptr);

    void drawDebug(const std::vector<Character*>& characters) const;
};
