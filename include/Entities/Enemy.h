#pragma once

#include <Entities/Character.h>
#include <Components/CombatComponent.h>
#include <raylib.h>

class Enemy : public Character {
public:
    Enemy(Vector3 start_position, Faction faction = Faction::Enemy);
    virtual ~Enemy() = default;

    virtual void update(float dt, const Vector3& player_position) = 0;
    virtual void draw() const override;
    void drawHPBar(const Camera3D& camera) const;
    
    std::vector<HurtBox> getHurtBoxes() const override;
    void takeDamage(float health_damage, float posture_damage) override;

    const CombatComponent& getCombatComponent() const { return combat_component; }

protected:
    CombatComponent combat_component;

    float body_height = 2.0f;
    float body_radius = 0.5f;
    Vector3 visual_size = { 1.0f, 1.0f, 1.0f };
    Color base_color = BLUE;
};