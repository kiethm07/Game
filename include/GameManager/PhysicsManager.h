#pragma once

#include <Components/WallObstacle.h>
#include <Components/Terrain.h>
#include <Entities/Character.h>
#include <Util/CollisionMath.h>
#include <raylib.h>
#include <vector>
#include <cassert>

enum class SurfaceType {
    Ground,
    Wall,
    Ceiling
};

class PhysicsManager {
public:
    PhysicsManager() = default;
    ~PhysicsManager() = default;

    SurfaceType classifySurfaceNormal(const Vector3& normal) const;

    void updatePhysics(const std::vector<Character*>& characters, const Terrain& terrain, const std::vector<WallObstacle>& walls, float dt);

    void resolveCharacterCollisions(const std::vector<Character*>& characters);
    void resolveEnvironmentCollisions(const std::vector<Character*>& characters, const std::vector<WallObstacle>& walls, const Terrain& terrain);
    void resolveGroundCollisions(const std::vector<Character*>& characters, const Terrain& terrain, const std::vector<WallObstacle>& walls, float dt);

    bool checkHeadroomClearance(const Character* character, float target_y, const std::vector<WallObstacle>& walls, const Terrain& terrain) const;

    void drawDebug(const std::vector<Character*>& characters, const std::vector<WallObstacle>& walls) const;
};
