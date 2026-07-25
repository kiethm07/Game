#include <Components/Terrain.h>
#include <raymath.h>
#include <algorithm>
#include <cmath>

TerrainRamp::TerrainRamp(Vector2 min_xz, Vector2 max_xz, float start_y, float end_y, Color color)
    : min_xz(min_xz), max_xz(max_xz), start_y(start_y), end_y(end_y), color(color)
{
}

bool TerrainRamp::contains(Vector3 position) const {
    if (position.x < min_xz.x || position.x > max_xz.x) {
        return false;
    }
    if (position.z < min_xz.y || position.z > max_xz.y) {
        return false;
    }

    float min_allowed_y = std::min(start_y, end_y) - 0.2f;
    float max_allowed_y = std::max(start_y, end_y) + 2.5f;

    if (position.y < min_allowed_y || position.y > max_allowed_y) {
        return false;
    }

    return true;
}

bool TerrainRamp::containsXZ(Vector3 position) const {
    if (position.x < min_xz.x || position.x > max_xz.x) {
        return false;
    }
    if (position.z < min_xz.y || position.z > max_xz.y) {
        return false;
    }
    return true;
}

float TerrainRamp::getHeightAt(Vector3 position) const {
    if (position.x < min_xz.x || position.x > max_xz.x || position.z < min_xz.y || position.z > max_xz.y) {
        return 0.0f;
    }

    float diff_z = max_xz.y - min_xz.y;
    float diff_x = max_xz.x - min_xz.x;

    float t = 0.0f;
    if (diff_z >= diff_x) {
        if (diff_z > 0.0001f) {
            t = (position.z - min_xz.y) / diff_z;
        }
    } else {
        if (diff_x > 0.0001f) {
            t = (position.x - min_xz.x) / diff_x;
        }
    }

    t = std::clamp(t, 0.0f, 1.0f);
    return start_y + t * (end_y - start_y);
}

Vector3 TerrainRamp::getNormal() const {
    float diff_z = max_xz.y - min_xz.y;
    float diff_x = max_xz.x - min_xz.x;
    float diff_y = end_y - start_y;

    Vector3 normal = { 0.0f, 1.0f, 0.0f };
    if (diff_z >= diff_x) {
        Vector3 n = { 0.0f, diff_z, -diff_y };
        normal = Vector3Normalize(n);
    } else {
        Vector3 n = { -diff_y, diff_x, 0.0f };
        normal = Vector3Normalize(n);
    }
    return normal;
}

bool TerrainRamp::resolveWall(Vector3& char_pos, float radius, float feet_y, float step_height) const {
    const float diff_z = max_xz.y - min_xz.y;
    const float diff_x = max_xz.x - min_xz.x;
    const float rise = end_y - start_y;
    if (std::fabs(rise) < 0.0001f) {
        return false;   // flat ramp: no wall
    }

    const bool rises_along_z = (diff_z >= diff_x);

    // Must overlap the ramp's XZ footprint (with a radius margin) to be blocked.
    // Both axes must be checked -- otherwise the ramp acts as an infinite strip
    // and yanks far-away characters toward it.
    if (char_pos.x < min_xz.x - radius || char_pos.x > max_xz.x + radius) return false;
    if (char_pos.z < min_xz.y - radius || char_pos.z > max_xz.y + radius) return false;

    // Sample the surface at the character's column (clamped into the footprint).
    Vector3 sample = char_pos;
    sample.x = std::clamp(sample.x, min_xz.x, max_xz.x);
    sample.z = std::clamp(sample.z, min_xz.y, max_xz.y);
    float surface = getHeightAt(sample);

    const float target = feet_y + step_height;   // highest surface we can stand on
    if (surface <= target) {
        return false;   // reachable: it's floor here, not a wall
    }

    // Contour along the rising axis where surface == target (the stand line).
    float t_b = std::clamp((target - start_y) / rise, 0.0f, 1.0f);

    if (rises_along_z) {
        float z_b = min_xz.y + t_b * diff_z;
        if (rise > 0.0f) {
            char_pos.z = std::min(char_pos.z, z_b - radius);   // surface higher toward +z
        } else {
            char_pos.z = std::max(char_pos.z, z_b + radius);
        }
    } else {
        float x_b = min_xz.x + t_b * diff_x;
        if (rise > 0.0f) {
            char_pos.x = std::min(char_pos.x, x_b - radius);
        } else {
            char_pos.x = std::max(char_pos.x, x_b + radius);
        }
    }
    return true;
}

bool TerrainRamp::isWalkable(float max_slope_angle) const {
    Vector3 normal = getNormal();
    float cos_limit = std::cos(max_slope_angle * DEG2RAD);
    return normal.y >= cos_limit;
}

void TerrainRamp::draw() const {
    Vector3 p0 = { min_xz.x, start_y, min_xz.y };
    Vector3 p1 = { max_xz.x, start_y, min_xz.y };
    Vector3 p2 = { max_xz.x, end_y, max_xz.y };
    Vector3 p3 = { min_xz.x, end_y, max_xz.y };

    DrawTriangle3D(p0, p2, p1, color);
    DrawTriangle3D(p0, p3, p2, color);
    DrawTriangle3D(p1, p2, p0, color);
    DrawTriangle3D(p2, p3, p0, color);

    DrawLine3D(p0, p1, BLACK);
    DrawLine3D(p1, p2, BLACK);
    DrawLine3D(p2, p3, BLACK);
    DrawLine3D(p3, p0, BLACK);
}

TerrainPlatform::TerrainPlatform(Vector3 min_corner, Vector3 max_corner, Color color)
    : color(color)
{
    box.min = min_corner;
    box.max = max_corner;
}

bool TerrainPlatform::contains(Vector3 position) const {
    if (position.x < box.min.x || position.x > box.max.x) {
        return false;
    }
    if (position.z < box.min.z || position.z > box.max.z) {
        return false;
    }
    return true;
}

void TerrainPlatform::draw() const {
    Vector3 size = Vector3Subtract(box.max, box.min);
    Vector3 center = Vector3Add(box.min, Vector3Scale(size, 0.5f));

    DrawCube(center, size.x, size.y, size.z, color);
    DrawCubeWires(center, size.x, size.y, size.z, BLACK);
}

void Terrain::addRamp(const TerrainRamp& ramp) {
    ramps.push_back(ramp);
}

void Terrain::addPlatform(const TerrainPlatform& platform) {
    platforms.push_back(platform);
}

float Terrain::getHeightAt(Vector3 position) const {
    float highest_y = base_ground_y;

    for (const TerrainPlatform& platform : platforms) {
        if (platform.contains(position)) {
            float top_y = platform.getTopY();
            if (top_y > highest_y) {
                highest_y = top_y;
            }
        }
    }

    for (const TerrainRamp& ramp : ramps) {
        if (ramp.contains(position)) {
            float ramp_y = ramp.getHeightAt(position);
            if (ramp_y > highest_y) {
                highest_y = ramp_y;
            }
        }
    }

    return highest_y;
}

GroundSample Terrain::sampleGround(Vector3 position, float step_height, float head_room, float prev_ground_y) const {
    const float feet = position.y;
    const float HYSTERESIS_EPS = 0.05f;   // "same layer as last frame" window

    GroundSample best{ base_ground_y, { 0.0f, 1.0f, 0.0f } };
    float best_score = -1e30f;

    // Rank candidates by top-height, but give a large bonus to whichever surface
    // matches the layer we were grounded on last frame so we don't flip layers.
    // `ceiling` is how far above the feet this surface may sit and still count as
    // floor: head_room for continuous ground (climb freely), step_height for
    // discrete ledges (only small step-ups).
    auto consider = [&](float top, Vector3 normal, float ceiling) {
        if (top > feet + ceiling) {
            return;   // too far above the feet -> ceiling or unclimbable ledge
        }
        float score = top;
        if (std::fabs(top - prev_ground_y) < HYSTERESIS_EPS) {
            score += 1000.0f;
        }
        if (score > best_score) {
            best_score = score;
            best.height = top;
            best.normal = normal;
        }
    };

    // The base ground plane is always the fallback candidate.
    consider(base_ground_y, { 0.0f, 1.0f, 0.0f }, head_room);

    for (const TerrainPlatform& platform : platforms) {
        if (platform.containsXZ(position)) {
            consider(platform.getTopY(), { 0.0f, 1.0f, 0.0f }, step_height);
        }
    }

    for (const TerrainRamp& ramp : ramps) {
        if (ramp.containsXZ(position)) {
            // A ramp is only floor where its surface is within a step of the
            // feet. Where it rises higher than that it is a wall, handled
            // separately by resolveWall() -- so it uses step_height here too.
            consider(ramp.getHeightAt(position), ramp.getNormal(), step_height);
        }
    }

    return best;
}

void Terrain::draw() const {
    for (const TerrainPlatform& platform : platforms) {
        platform.draw();
    }
    for (const TerrainRamp& ramp : ramps) {
        ramp.draw();
    }
}
