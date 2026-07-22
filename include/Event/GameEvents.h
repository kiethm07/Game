#pragma once
#include <raymath.h>

class AttackActiveEvent {
private:
    unsigned int attacker_id;
    Vector3 attack_position;
    Vector3 attack_direction;
    float health_damage;
    float posture_damage;

public:
    AttackActiveEvent(unsigned int attacker_id, Vector3 attack_position, 
                      Vector3 attack_direction, float health_damage, float posture_damage)
        : attacker_id(attacker_id), 
          attack_position(attack_position), 
          attack_direction(attack_direction), 
          health_damage(health_damage), 
          posture_damage(posture_damage) {}

    unsigned int getAttackerId() const { return attacker_id; }
    Vector3 getAttackPosition() const { return attack_position; }
    Vector3 getAttackDirection() const { return attack_direction; }
    float getHealthDamage() const { return health_damage; }
    float getPostureDamage() const { return posture_damage; }
};