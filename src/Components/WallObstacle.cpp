#include <Components/WallObstacle.h>

WallObstacle::WallObstacle(Vector3 min_corner, Vector3 max_corner, Color color)
    : color(color)
{
    box.min = min_corner;
    box.max = max_corner;
}
