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

void Terrain::draw() const {
    for (const TerrainPlatform& platform : platforms) {
        platform.draw();
    }
    for (const TerrainRamp& ramp : ramps) {
        ramp.draw();
    }
}
