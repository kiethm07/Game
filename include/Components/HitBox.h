#pragma once
#include <Components/Faction.h>
#include <Util/Sphere.h>
#include <Util/Capsule.h>
#include "raylib.h"
#include <variant>
#include <raymath.h>

class HitBox {
public:
    HitBox(std::variant<Sphere, Capsule> shape, float health_damage, float posture_damage, Faction faction, unsigned int attacker_id)
        : shape(shape), 
          health_damage(health_damage), 
          posture_damage(posture_damage), 
          faction(faction), 
          attacker_id(attacker_id) {}

    bool isSphere() const { return std::holds_alternative<Sphere>(shape); }
    bool isCapsule() const { return std::holds_alternative<Capsule>(shape); }

    Sphere getSphere() const { return std::get<Sphere>(shape); }
    Capsule getCapsule() const { return std::get<Capsule>(shape); }

    Vector3 getCenter() const {
        if (isSphere()) return getSphere().getCenter();
        Capsule c = getCapsule();
        return Vector3Scale(Vector3Add(c.getBase(), c.getTip()), 0.5f);
    }
    float getHealthDamage() const { return health_damage; }
    float getPostureDamage() const { return posture_damage; }
    Faction getFaction() const { return faction; }
    unsigned int getAttackerId() const { return attacker_id; }

private:
    std::variant<Sphere, Capsule> shape;
    float health_damage;
    float posture_damage;
    Faction faction;
    unsigned int attacker_id;
};