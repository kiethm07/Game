#pragma once

#include <raylib.h>
#include <vector>

enum class ObstacleShape {
    BOX_SHAPE,
    RAMP_SHAPE
};

// Unified geometry primitive used by PhysicsManager.
// A BOX_SHAPE represents axis-aligned boxes (walls, platforms, pillars).
// A RAMP_SHAPE represents a linearly-sloped quad defined in XZ + two Y endpoints.
//
// Physics behavior is determined dynamically from surface normals, NOT from
// the shape tag — the tag is only used to route geometry queries.
class PhysicsObstacle {
public:
    // Construct a box obstacle (wall, platform, or any axis-aligned box).
    PhysicsObstacle(Vector3 min_corner, Vector3 max_corner, float yaw = 0.0f, Color color = DARKGRAY);

    // Construct a ramp obstacle sloping from start_y (at min_xz) to end_y (at max_xz).
    PhysicsObstacle(Vector2 min_xz, Vector2 max_xz, float start_y, float end_y, float yaw = 0.0f, Color color = BEIGE);

    ObstacleShape getShape() const { return shape; }

    // Matrices for Local/World space transforms
    Matrix getWorldToLocal() const { return worldToLocal; }
    Matrix getLocalToWorld() const { return localToWorld; }

    // Returns the obstacle's bounding box in its LOCAL coordinate space.
    // For BOX_SHAPE, this defines the exact dimensions.
    // For RAMP_SHAPE, this bounds the sloped quad in local space.
    const BoundingBox& getLocalBox() const { return local_box; }

    // Returns a world-space AABB that loosely encloses the entire rotated obstacle.
    // Used for broad-phase culling.
    BoundingBox getApproxBox() const;

    // Returns the surface Y height at `position` for a valid foothold.
    // The position is transformed to local space internally.
    float getHeightAt(Vector3 position) const;

    // Returns the outward surface normal of the top face in WORLD space.
    Vector3 getNormal() const;

    // Returns true if `position.x` and `position.z` are within the obstacle's XZ footprint.
    // Expanded by radius to keep character grounded if any part of their cylinder overlaps.
    bool containsXZ(Vector3 position, float radius = 0.0f) const;

    Color getColor() const { return color; }
    void draw() const;

private:
    ObstacleShape shape;

    float yaw;
    Matrix localToWorld;
    Matrix worldToLocal;

    // The AABB of the obstacle in its LOCAL space (centered at {0,0,0}).
    BoundingBox local_box;

    // RAMP_SHAPE local geometry (Y-axis points are fixed relative to local space)
    Vector2 ramp_min_xz;
    Vector2 ramp_max_xz;
    float   ramp_start_y;
    float   ramp_end_y;

    Color color;
};
