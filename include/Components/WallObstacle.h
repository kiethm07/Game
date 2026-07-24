#pragma once

#include <raylib.h>

class WallObstacle {
public:
    WallObstacle(Vector3 min_corner, Vector3 max_corner, Color color = DARKGRAY);

    const BoundingBox& getBox() const { return box; }
    Color getColor() const { return color; }

private:
    BoundingBox box;
    Color color;
};
