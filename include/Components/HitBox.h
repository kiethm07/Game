#pragma once
#include <Components/Faction.h>
#include <Util/Sphere.h>
#include "raylib.h"

class HitBox {
public:
    HitBox(Sphere shape, float health_damage, float posture_damage, Faction faction, unsigned int attacker_id)
        : shape(shape), 
          health_damage(health_damage), 
          posture_damage(posture_damage), 
          faction(faction), 
          attacker_id(attacker_id) {}

    Sphere getShape() const { return shape; }
    float getHealthDamage() const { return health_damage; }
    float getPostureDamage() const { return posture_damage; }
    Faction getFaction() const { return faction; }
    unsigned int getAttackerId() const { return attacker_id; }

private:
    Sphere shape;
    float health_damage;
    float posture_damage;
    Faction faction;
    unsigned int attacker_id;
};