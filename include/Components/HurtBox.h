#pragma once
#include <Components/Faction.h>
#include <Util/Capsule.h>
#include "raylib.h"

class HurtBox {
public:
    HurtBox(Capsule shape, Faction faction, unsigned int owner_id)
            : shape(shape), faction(faction), owner_id(owner_id) {}

    Capsule getShape() const { return shape; }
    Faction getFaction() const { return faction; }
    unsigned int getOwnerId() const { return owner_id; }

private:
    Capsule shape;
    Faction faction;
    unsigned int owner_id;
};