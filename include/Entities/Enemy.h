#pragma once

#include <Entities/Character.h>
#include <Components/CombatComponent.h>

class Enemy : public Character {
public:
    Enemy(Vector3 start_position, Faction faction = Faction::Enemy);
    virtual ~Enemy() = default;

    virtual void update(float dt, const Vector3& player_position) = 0;

protected:
    CombatComponent combat_component;
};