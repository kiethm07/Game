#pragma once

#include <raylib.h>

class Capsule {
private:
    Vector3 base; // Center of bottom hemisphere (feet)
    Vector3 tip;  // Center of top hemisphere (head)
    float radius;

public:
    Capsule(Vector3 base, Vector3 tip, float radius)
        : base(base), tip(tip), radius(radius) {}

    static Capsule createUpright(Vector3 position, float total_height, float radius) {
        float inner_height = total_height - (2.0f * radius);
        if (inner_height < 0.0f) inner_height = 0.0f;

        Vector3 base_pt = { position.x, position.y + radius, position.z };
        Vector3 tip_pt  = { position.x, position.y + radius + inner_height, position.z };
        return Capsule(base_pt, tip_pt, radius);
    }

    Vector3 getBase() const { return base; }
    Vector3 getTip() const { return tip; }
    float getRadius() const { return radius; }
};