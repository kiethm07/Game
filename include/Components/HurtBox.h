#pragma once
#include <Components/Faction.h>
#include "raylib.h"

class HurtBox {
public:
    HurtBox(Vector3 center, float radius, Faction faction, unsigned int owner_id)
        : center(center), radius(radius), faction(faction), owner_id(owner_id) {}

    Vector3 getCenter() const { return center; }
    float getRadius() const { return radius; }
    Faction getFaction() const { return faction; }
    unsigned int getOwnerId() const { return owner_id; }

private:
    Vector3 center;
    float radius;
    Faction faction;
    unsigned int owner_id;
};