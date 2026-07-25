#pragma once

#include <raylib.h>
#include <vector>

class TerrainRamp {
public:
    TerrainRamp(Vector2 min_xz, Vector2 max_xz, float start_y, float end_y, Color color = GRAY);

    bool contains(Vector3 position) const;
    // XZ-only footprint test. Unlike contains(), this ignores position.y so it
    // can be used to sample the surface height without a chicken-and-egg Y gate.
    bool containsXZ(Vector3 position) const;
    float getHeightAt(Vector3 position) const;
    Vector3 getNormal() const;
    bool isWalkable(float max_slope_angle = 45.0f) const;
    // Horizontal (wall) collision for the solid part of the ramp. If the feet
    // are more than step_height below the ramp surface at char_pos, the ramp is
    // acting as a wall here: push char_pos horizontally down-slope to the
    // contour it can actually stand on. Returns true if it moved char_pos.
    bool resolveWall(Vector3& char_pos, float radius, float feet_y, float step_height) const;
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
    bool containsXZ(Vector3 position) const { return contains(position); }
    float getTopY() const { return box.max.y; }
    const BoundingBox& getBox() const { return box; }
    Color getColor() const { return color; }
    void draw() const;

private:
    BoundingBox box;
    Color color;
};

// Result of resolving which surface supports the character at a given column.
struct GroundSample {
    float height;     // top-Y of the supporting surface
    Vector3 normal;   // surface normal (used to tell floor from slope from wall)
};

class Terrain {
public:
    Terrain() = default;
    ~Terrain() = default;

    void addRamp(const TerrainRamp& ramp);
    void addPlatform(const TerrainPlatform& platform);

    float getHeightAt(Vector3 position) const;

    // Layer-aware ground query. Of all surfaces in this XZ column, returns the
    // one the character is actually standing on. Continuous surfaces (ramps, the
    // base plane) are accepted as floor whenever they sit at or below head_room
    // above the feet -- you are always meant to be on a ramp, so no step limit
    // applies. Discrete ledges (platform tops) are only accepted within
    // step_height above the feet, so a tall platform edge does not yank you up.
    // Anything above head_room is a ceiling and ignored. prev_ground_y biases
    // selection toward the layer we were already on to prevent seam flicker.
    GroundSample sampleGround(Vector3 position, float step_height, float head_room, float prev_ground_y) const;
    const std::vector<TerrainPlatform>& getPlatforms() const { return platforms; }
    const std::vector<TerrainRamp>& getRamps() const { return ramps; }

    void draw() const;

private:
    std::vector<TerrainRamp> ramps;
    std::vector<TerrainPlatform> platforms;
    float base_ground_y = 0.0f;
};
