#pragma once

#include <Components/PhysicsObstacle.h>
#include <Entities/Character.h>
#include <Physics/CollisionMesh.h>
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

    /// `mesh` may be null, and usually is for a greybox: a level whose world is
    /// simple enough to be boxes and ramps ships no triangle collision, and
    /// every mesh query below is skipped. The obstacle path is untouched by
    /// this, so those levels behave exactly as they did.
    std::vector<Vector3> updatePhysics(
        const std::vector<Character*>& characters,
        const std::vector<PhysicsObstacle>& obstacles,
        const CollisionMesh* mesh,
        float dt);

    void resolveCharacterCollisions(const std::vector<Character*>& characters, std::vector<Vector3>& positions);

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
        const std::vector<PhysicsObstacle>& obstacles,
        const CollisionMesh* mesh = nullptr) const;

    void drawDebug(
        const std::vector<Character*>& characters,
        const std::vector<PhysicsObstacle>& obstacles) const;

private:
    /// Highest supporting surface under `pos` that the character could step
    /// onto, found by probing the mesh downward from the step ceiling.
    ///
    /// Probes at the centre and at four offsets around it rather than once,
    /// mirroring the GROUND_PROBE_REACH expansion the obstacle path applies to
    /// its XZ footprint test. A single central ray drops the character the
    /// instant their centre clears a ledge, which reads as falling off stairs
    /// that are visibly still underfoot.
    bool probeMeshGround(const CollisionMesh* mesh, Vector3 pos, float radius,
                         float max_step, float& out_y,
                         Vector3& out_normal) const;

    /// Scratch buffer for CollisionMesh::overlapAABB, kept across calls so the
    /// per-substep triangle queries stop reallocating.
    mutable std::vector<int> triangle_scratch;
};
