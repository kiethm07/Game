#pragma once

#include <Components/PhysicsObstacle.h>
#include <Entities/Character.h>
#include <Util/CollisionMath.h>
#include <raylib.h>
#include <vector>
#include <cassert>

enum class SurfaceType {
    GROUND_SURF,
    WALL_SURF,
    CEILING_SURF
};

class PhysicsManager {
public:
    PhysicsManager()  = default;
    ~PhysicsManager() = default;

    SurfaceType classifySurfaceNormal(const Vector3& normal) const;

    void updatePhysics(
        const std::vector<Character*>& characters,
        const std::vector<PhysicsObstacle>& obstacles,
        float dt);

    void resolveCharacterCollisions(const std::vector<Character*>& characters);

    void resolveEnvironmentCollisions(
        const std::vector<Character*>& characters,
        const std::vector<PhysicsObstacle>& obstacles);

    void resolveGroundCollisions(
        const std::vector<Character*>& characters,
        const std::vector<PhysicsObstacle>& obstacles,
        float dt);

    bool checkHeadroomClearance(
        Vector3 current_pos,
        float   radius,
        float   height,
        float   target_y,
        const std::vector<PhysicsObstacle>& obstacles) const;

    void drawDebug(
        const std::vector<Character*>& characters,
        const std::vector<PhysicsObstacle>& obstacles) const;
};
