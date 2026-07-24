#pragma once

#include <raylib.h>
#include <vector>

class TerrainRamp {
public:
    TerrainRamp(Vector2 min_xz, Vector2 max_xz, float start_y, float end_y, Color color = GRAY);

    bool contains(Vector3 position) const;
    float getHeightAt(Vector3 position) const;
    Vector3 getNormal() const;
    bool isWalkable(float max_slope_angle = 45.0f) const;
    void draw() const;

    Vector2 getMinXZ() const { return min_xz; }
    Vector2 getMaxXZ() const { return max_xz; }
    float getStartY() const { return start_y; }
    float getEndY() const { return end_y; }
    Color getColor() const { return color; }

private:
    Vector2 min_xz;
    Vector2 max_xz;
    float start_y;
    float end_y;
    Color color;
};

class TerrainPlatform {
public:
    TerrainPlatform(Vector3 min_corner, Vector3 max_corner, Color color = DARKGRAY);

    bool contains(Vector3 position) const;
    float getTopY() const { return box.max.y; }
    const BoundingBox& getBox() const { return box; }
    Color getColor() const { return color; }
    void draw() const;

private:
    BoundingBox box;
    Color color;
};

class Terrain {
public:
    Terrain() = default;
    ~Terrain() = default;

    void addRamp(const TerrainRamp& ramp);
    void addPlatform(const TerrainPlatform& platform);

    float getHeightAt(Vector3 position) const;
    const std::vector<TerrainPlatform>& getPlatforms() const { return platforms; }
    const std::vector<TerrainRamp>& getRamps() const { return ramps; }

    void draw() const;

private:
    std::vector<TerrainRamp> ramps;
    std::vector<TerrainPlatform> platforms;
    float base_ground_y = 0.0f;
};
