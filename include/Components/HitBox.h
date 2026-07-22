#pragma once
#include <Components/Faction.h>
#include "raylib.h"

class HitBox {
public:
    HitBox(Vector3 center, float radius, float health_damage, float posture_damage, Faction faction, unsigned int attacker_id)
        : center(center), 
          radius(radius), 
          health_damage(health_damage), 
          posture_damage(posture_damage), 
          faction(faction), 
          attacker_id(attacker_id) {}

    Vector3 getCenter() const { return center; }
    float getRadius() const { return radius; }
    float getHealthDamage() const { return health_damage; }
    float getPostureDamage() const { return posture_damage; }
    Faction getFaction() const { return faction; }
    unsigned int getAttackerId() const { return attacker_id; }

private:
    Vector3 center;
    float radius;
    float health_damage;
    float posture_damage;
    Faction faction;
    unsigned int attacker_id;
};