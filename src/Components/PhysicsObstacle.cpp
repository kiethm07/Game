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

    // Raymath's MatrixMultiply(left, right) applies LEFT FIRST -- check the
    // translation column of the product in raymath.h: for left=Translate(c) it
    // comes out as R(c), i.e. the result is R * T. So rotate-then-translate
    // (v' = T * R * v) is MatrixMultiply(Rotate, Translate).
    //
    // Passing them the other way round spins the obstacle about the WORLD
    // origin instead of its own centre, which silently relocates every obstacle
    // with a non-zero yaw -- and does it to the collision volume and the drawn
    // volume alike, so it reads as the level being authored wrong.
    localToWorld = MatrixMultiply(MatrixRotateY(yaw * DEG2RAD), MatrixTranslate(center.x, center.y, center.z));
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

    // Rotate then translate; see the box constructor for why the arguments are
    // in this order.
    localToWorld = MatrixMultiply(MatrixRotateY(yaw * DEG2RAD), MatrixTranslate(center.x, center.y, center.z));
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

    // Outside the footprint the answer is the height at the nearest edge, not an
    // extrapolation. A ramp sampled past its own side otherwise reports the slope
    // it WOULD have at that depth, so a caller probing for ground beside a
    // staircase is handed a surface that climbs as it walks along it — an
    // invisible incline running parallel to the visible one. A box is flat, so
    // this changes nothing for BOX_SHAPE beyond making the contract explicit.
    local_pos.x = std::clamp(local_pos.x, local_box.min.x, local_box.max.x);
    local_pos.z = std::clamp(local_pos.z, local_box.min.z, local_box.max.z);

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

Vector3 PhysicsObstacle::getLocalNormal() const {
    if (shape == ObstacleShape::BOX_SHAPE) {
        return { 0.0f, 1.0f, 0.0f };
    }

    float dz = ramp_max_xz.y - ramp_min_xz.y;
    float dx = ramp_max_xz.x - ramp_min_xz.x;
    float dy = ramp_end_y - ramp_start_y;

    Vector3 raw;
    if (std::abs(dz) >= std::abs(dx)) {
        raw = { 0.0f, dz, -dy };
    } else {
        raw = { -dy, std::abs(dx), 0.0f };
    }
    return Vector3Normalize(raw);
}

Vector3 PhysicsObstacle::getNormal() const {
    // Transform normal to world space (rotation only)
    Matrix rot_only = MatrixRotateY(yaw * DEG2RAD);
    return Vector3Transform(getLocalNormal(), rot_only);
}

bool PhysicsObstacle::containsXZ(Vector3 position, float radius) const {
    Vector3 local_pos = Vector3Transform(position, worldToLocal);
    
    // Check against local bounding box footprint
    return local_pos.x + radius >= local_box.min.x && local_pos.x - radius <= local_box.max.x &&
           local_pos.z + radius >= local_box.min.z && local_pos.z - radius <= local_box.max.z;
}

// Local geometry is submitted under rlMultMatrixf(localToWorld). rlgl applies
// that transform on the CPU as each vertex and normal is submitted, so
// everything reaches the shader already in world space — which is exactly what
// world.vs and depth.vs expect. See the comment at the top of world.vs.
void PhysicsObstacle::pushTransform() const {
    rlPushMatrix();

    float mat[16] = {
        localToWorld.m0, localToWorld.m1, localToWorld.m2, localToWorld.m3,
        localToWorld.m4, localToWorld.m5, localToWorld.m6, localToWorld.m7,
        localToWorld.m8, localToWorld.m9, localToWorld.m10, localToWorld.m11,
        localToWorld.m12, localToWorld.m13, localToWorld.m14, localToWorld.m15
    };
    rlMultMatrixf(mat);
}

void PhysicsObstacle::rampCorners(Vector3 top[4], Vector3 bottom[4]) const {
    auto getLocalY = [&](float x, float z) {
        float dz = ramp_max_xz.y - ramp_min_xz.y;
        float dx = ramp_max_xz.x - ramp_min_xz.x;
        float t = 0.0f;
        if (std::abs(dz) >= std::abs(dx)) t = (std::abs(dz)>0.0001f)? (z - ramp_min_xz.y)/dz : 0.0f;
        else t = (std::abs(dx)>0.0001f)? (x - ramp_min_xz.x)/dx : 0.0f;
        return ramp_start_y + t * (ramp_end_y - ramp_start_y);
    };

    top[0] = { ramp_min_xz.x, getLocalY(ramp_min_xz.x, ramp_min_xz.y), ramp_min_xz.y };
    top[1] = { ramp_max_xz.x, getLocalY(ramp_max_xz.x, ramp_min_xz.y), ramp_min_xz.y };
    top[2] = { ramp_max_xz.x, getLocalY(ramp_max_xz.x, ramp_max_xz.y), ramp_max_xz.y };
    top[3] = { ramp_min_xz.x, getLocalY(ramp_min_xz.x, ramp_max_xz.y), ramp_max_xz.y };

    float b_y = std::min(ramp_start_y, ramp_end_y);
    bottom[0] = { ramp_min_xz.x, b_y, ramp_min_xz.y };
    bottom[1] = { ramp_max_xz.x, b_y, ramp_min_xz.y };
    bottom[2] = { ramp_max_xz.x, b_y, ramp_max_xz.y };
    bottom[3] = { ramp_min_xz.x, b_y, ramp_max_xz.y };
}

void PhysicsObstacle::drawSolid() const {
    pushTransform();

    if (shape == ObstacleShape::BOX_SHAPE) {
        Vector3 size   = Vector3Subtract(local_box.max, local_box.min);
        Vector3 center = Vector3Add(local_box.min, Vector3Scale(size, 0.5f));
        // DrawCube emits its own rlNormal3f per face, so boxes light correctly
        // with no help from us.
        DrawCube(center, size.x, size.y, size.z, color);
    } else {
        Vector3 t[4], b[4];
        rampCorners(t, b);

        // DrawTriangle3D submits no normal of its own — it inherits whatever
        // rlNormal3f last set, which before this was leftover state from some
        // unrelated draw. Set one per face, in LOCAL space: rlNormal3f applies
        // the rotation part of the transform pushed above.
        //
        // The windings below were already correct for backface culling; the
        // normals here match them, outward in every case.

        // Top (the slope itself)
        Vector3 slope = getLocalNormal();
        rlNormal3f(slope.x, slope.y, slope.z);
        DrawTriangle3D(t[0], t[2], t[1], color); DrawTriangle3D(t[0], t[3], t[2], color);
        // Bottom
        rlNormal3f(0.0f, -1.0f, 0.0f);
        DrawTriangle3D(b[0], b[1], b[2], color); DrawTriangle3D(b[0], b[2], b[3], color);
        // Front (min z)
        rlNormal3f(0.0f, 0.0f, -1.0f);
        DrawTriangle3D(b[0], t[1], b[1], color); DrawTriangle3D(b[0], t[0], t[1], color);
        // Back (max z)
        rlNormal3f(0.0f, 0.0f, 1.0f);
        DrawTriangle3D(b[2], t[2], b[3], color); DrawTriangle3D(b[3], t[2], t[3], color);
        // Left (min x)
        rlNormal3f(-1.0f, 0.0f, 0.0f);
        DrawTriangle3D(b[3], t[3], b[0], color); DrawTriangle3D(b[0], t[3], t[0], color);
        // Right (max x)
        rlNormal3f(1.0f, 0.0f, 0.0f);
        DrawTriangle3D(b[1], t[1], b[2], color); DrawTriangle3D(b[2], t[1], t[2], color);
    }

    rlPopMatrix();
}

void PhysicsObstacle::drawWires() const {
    pushTransform();

    if (shape == ObstacleShape::BOX_SHAPE) {
        Vector3 size   = Vector3Subtract(local_box.max, local_box.min);
        Vector3 center = Vector3Add(local_box.min, Vector3Scale(size, 0.5f));
        DrawCubeWires(center, size.x, size.y, size.z, BLACK);
    } else {
        Vector3 t[4], b[4];
        rampCorners(t, b);

        DrawLine3D(t[0], t[1], BLACK); DrawLine3D(t[1], t[2], BLACK);
        DrawLine3D(t[2], t[3], BLACK); DrawLine3D(t[3], t[0], BLACK);
        DrawLine3D(b[0], b[1], BLACK); DrawLine3D(b[1], b[2], BLACK);
        DrawLine3D(b[2], b[3], BLACK); DrawLine3D(b[3], b[0], BLACK);
        DrawLine3D(t[0], b[0], BLACK); DrawLine3D(t[1], b[1], BLACK);
        DrawLine3D(t[2], b[2], BLACK); DrawLine3D(t[3], b[3], BLACK);
    }

    rlPopMatrix();
}
