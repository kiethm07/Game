#pragma once

#include <raylib.h>

class Sphere {
private:
    Vector3 center;
    float radius;

public:
    Sphere(Vector3 center, float radius)
        : center(center), radius(radius) {}

    Vector3 getCenter() const { return center; }
    float getRadius() const { return radius; }
};