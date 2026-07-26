#include <Components/PhysicsObstacle.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <cmath>

// --- BOX_SHAPE constructor ---
PhysicsObstacle::PhysicsObstacle(Vector3 min_corner, Vector3 max_corner, float yaw, Color color)
    : shape(ObstacleShape::BOX_SHAPE),
      yaw(yaw),
      ramp_min_xz({0.0f, 0.0f}),
      ramp_max_xz({0.0f, 0.0f}),
      ramp_start_y(0.0f),
      ramp_end_y(0.0f),
      color(color)
{
    Vector3 center = Vector3Scale(Vector3Add(min_corner, max_corner), 0.5f);
    Vector3 extents = Vector3Scale(Vector3Subtract(max_corner, min_corner), 0.5f);
    
    local_box.min = Vector3Negate(extents);
    local_box.max = extents;

    // Raymath's MatrixMultiply(left, right) evaluates left * right.
    // To construct a matrix that Rotates first, then Translates (v' = T * R * v),
    // we need MatrixMultiply(Translate, Rotate).
    localToWorld = MatrixMultiply(MatrixTranslate(center.x, center.y, center.z), MatrixRotateY(yaw * DEG2RAD));
    worldToLocal = MatrixInvert(localToWorld);
}

// --- RAMP_SHAPE constructor ---
PhysicsObstacle::PhysicsObstacle(Vector2 min_xz, Vector2 max_xz, float start_y, float end_y, float yaw, Color color)
    : shape(ObstacleShape::RAMP_SHAPE),
      yaw(yaw),
      local_box({{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}}),
      color(color)
{
    Vector3 center = {
        (min_xz.x + max_xz.x) * 0.5f,
        (start_y + end_y) * 0.5f,
        (min_xz.y + max_xz.y) * 0.5f
    };

    ramp_min_xz = { min_xz.x - center.x, min_xz.y - center.z };
    ramp_max_xz = { max_xz.x - center.x, max_xz.y - center.z };
    ramp_start_y = start_y - center.y;
    ramp_end_y = end_y - center.y;

    float min_y = std::min(ramp_start_y, ramp_end_y);
    float max_y = std::max(ramp_start_y, ramp_end_y);

    local_box.min = { std::min(ramp_min_xz.x, ramp_max_xz.x), min_y, std::min(ramp_min_xz.y, ramp_max_xz.y) };
    local_box.max = { std::max(ramp_min_xz.x, ramp_max_xz.x), max_y, std::max(ramp_min_xz.y, ramp_max_xz.y) };

    localToWorld = MatrixMultiply(MatrixTranslate(center.x, center.y, center.z), MatrixRotateY(yaw * DEG2RAD));
    worldToLocal = MatrixInvert(localToWorld);
}

BoundingBox PhysicsObstacle::getApproxBox() const {
    // Transform all 8 corners of local_box to world space and find AABB
    Vector3 corners[8] = {
        { local_box.min.x, local_box.min.y, local_box.min.z },
        { local_box.max.x, local_box.min.y, local_box.min.z },
        { local_box.min.x, local_box.max.y, local_box.min.z },
        { local_box.max.x, local_box.max.y, local_box.min.z },
        { local_box.min.x, local_box.min.y, local_box.max.z },
        { local_box.max.x, local_box.min.y, local_box.max.z },
        { local_box.min.x, local_box.max.y, local_box.max.z },
        { local_box.max.x, local_box.max.y, local_box.max.z }
    };

    BoundingBox approx;
    approx.min = { 99999.0f, 99999.0f, 99999.0f };
    approx.max = { -99999.0f, -99999.0f, -99999.0f };

    for (int i = 0; i < 8; ++i) {
        Vector3 world_corner = Vector3Transform(corners[i], localToWorld);
        approx.min.x = std::min(approx.min.x, world_corner.x);
        approx.min.y = std::min(approx.min.y, world_corner.y);
        approx.min.z = std::min(approx.min.z, world_corner.z);
        
        approx.max.x = std::max(approx.max.x, world_corner.x);
        approx.max.y = std::max(approx.max.y, world_corner.y);
        approx.max.z = std::max(approx.max.z, world_corner.z);
    }
    return approx;
}

float PhysicsObstacle::getHeightAt(Vector3 position) const {
    Vector3 local_pos = Vector3Transform(position, worldToLocal);

    if (shape == ObstacleShape::BOX_SHAPE) {
        // Return max.y transformed back to world
        Vector3 top_local = { local_pos.x, local_box.max.y, local_pos.z };
        return Vector3Transform(top_local, localToWorld).y;
    }

    // Linear interpolation along the dominant slope axis in LOCAL space
    float diff_z = ramp_max_xz.y - ramp_min_xz.y;
    float diff_x = ramp_max_xz.x - ramp_min_xz.x;

    float t = 0.0f;
    if (std::abs(diff_z) >= std::abs(diff_x)) {
        if (std::abs(diff_z) > 0.0001f) {
            t = (local_pos.z - ramp_min_xz.y) / diff_z;
        }
    } else {
        if (std::abs(diff_x) > 0.0001f) {
            t = (local_pos.x - ramp_min_xz.x) / diff_x;
        }
    }

    t = std::clamp(t, 0.0f, 1.0f);
    float local_y = ramp_start_y + t * (ramp_end_y - ramp_start_y);
    Vector3 surface_local = { local_pos.x, local_y, local_pos.z };
    return Vector3Transform(surface_local, localToWorld).y;
}

Vector3 PhysicsObstacle::getNormal() const {
    Vector3 local_normal;
    if (shape == ObstacleShape::BOX_SHAPE) {
        local_normal = { 0.0f, 1.0f, 0.0f };
    } else {
        float dz = ramp_max_xz.y - ramp_min_xz.y;
        float dx = ramp_max_xz.x - ramp_min_xz.x;
        float dy = ramp_end_y - ramp_start_y;

        Vector3 raw;
        if (std::abs(dz) >= std::abs(dx)) {
            raw = { 0.0f, dz, -dy };
        } else {
            raw = { -dy, std::abs(dx), 0.0f };
        }
        local_normal = Vector3Normalize(raw);
    }

    // Transform normal to world space (rotation only)
    Matrix rot_only = MatrixRotateY(yaw * DEG2RAD);
    return Vector3Transform(local_normal, rot_only);
}

bool PhysicsObstacle::containsXZ(Vector3 position, float radius) const {
    Vector3 local_pos = Vector3Transform(position, worldToLocal);
    
    // Check against local bounding box footprint
    return local_pos.x + radius >= local_box.min.x && local_pos.x - radius <= local_box.max.x &&
           local_pos.z + radius >= local_box.min.z && local_pos.z - radius <= local_box.max.z;
}

void PhysicsObstacle::draw() const {
    rlPushMatrix();
    
    float mat[16] = {
        localToWorld.m0, localToWorld.m1, localToWorld.m2, localToWorld.m3,
        localToWorld.m4, localToWorld.m5, localToWorld.m6, localToWorld.m7,
        localToWorld.m8, localToWorld.m9, localToWorld.m10, localToWorld.m11,
        localToWorld.m12, localToWorld.m13, localToWorld.m14, localToWorld.m15
    };
    rlMultMatrixf(mat);

    if (shape == ObstacleShape::BOX_SHAPE) {
        Vector3 size   = Vector3Subtract(local_box.max, local_box.min);
        Vector3 center = Vector3Add(local_box.min, Vector3Scale(size, 0.5f));
        DrawCube(center, size.x, size.y, size.z, color);
        DrawCubeWires(center, size.x, size.y, size.z, BLACK);
    } else {
        auto getLocalY = [&](float x, float z) {
            float dz = ramp_max_xz.y - ramp_min_xz.y;
            float dx = ramp_max_xz.x - ramp_min_xz.x;
            float t = 0.0f;
            if (std::abs(dz) >= std::abs(dx)) t = (std::abs(dz)>0.0001f)? (z - ramp_min_xz.y)/dz : 0.0f;
            else t = (std::abs(dx)>0.0001f)? (x - ramp_min_xz.x)/dx : 0.0f;
            return ramp_start_y + t * (ramp_end_y - ramp_start_y);
        };

        // Top corners
        Vector3 t0 = { ramp_min_xz.x, getLocalY(ramp_min_xz.x, ramp_min_xz.y), ramp_min_xz.y };
        Vector3 t1 = { ramp_max_xz.x, getLocalY(ramp_max_xz.x, ramp_min_xz.y), ramp_min_xz.y };
        Vector3 t2 = { ramp_max_xz.x, getLocalY(ramp_max_xz.x, ramp_max_xz.y), ramp_max_xz.y };
        Vector3 t3 = { ramp_min_xz.x, getLocalY(ramp_min_xz.x, ramp_max_xz.y), ramp_max_xz.y };

        // Bottom corners (min_y)
        float b_y = std::min(ramp_start_y, ramp_end_y);
        Vector3 b0 = { ramp_min_xz.x, b_y, ramp_min_xz.y };
        Vector3 b1 = { ramp_max_xz.x, b_y, ramp_min_xz.y };
        Vector3 b2 = { ramp_max_xz.x, b_y, ramp_max_xz.y };
        Vector3 b3 = { ramp_min_xz.x, b_y, ramp_max_xz.y };

        // Draw 6 faces (two triangles each)
        // Top
        DrawTriangle3D(t0, t2, t1, color); DrawTriangle3D(t0, t3, t2, color);
        // Bottom
        DrawTriangle3D(b0, b1, b2, color); DrawTriangle3D(b0, b2, b3, color);
        // Front (min z)
        DrawTriangle3D(b0, t1, b1, color); DrawTriangle3D(b0, t0, t1, color);
        // Back (max z)
        DrawTriangle3D(b2, t2, b3, color); DrawTriangle3D(b3, t2, t3, color);
        // Left (min x)
        DrawTriangle3D(b3, t3, b0, color); DrawTriangle3D(b0, t3, t0, color);
        // Right (max x)
        DrawTriangle3D(b1, t1, b2, color); DrawTriangle3D(b2, t1, t2, color);

        // Edges
        DrawLine3D(t0, t1, BLACK); DrawLine3D(t1, t2, BLACK); DrawLine3D(t2, t3, BLACK); DrawLine3D(t3, t0, BLACK);
        DrawLine3D(b0, b1, BLACK); DrawLine3D(b1, b2, BLACK); DrawLine3D(b2, b3, BLACK); DrawLine3D(b3, b0, BLACK);
        DrawLine3D(t0, b0, BLACK); DrawLine3D(t1, b1, BLACK); DrawLine3D(t2, b2, BLACK); DrawLine3D(t3, b3, BLACK);
    }

    rlPopMatrix();
}
