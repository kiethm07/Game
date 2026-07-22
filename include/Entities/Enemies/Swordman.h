#pragma once

#include <Entities/Enemy.h>
#include <CombatData/Combo.h>

class Swordman : public Enemy {
public:
    Swordman(Vector3 start_position);
    ~Swordman() override = default;

    void update(float dt, const Vector3& player_position) override;
    void draw() const override;

    HurtBox getHurtBox() const override;
    std::vector<HitBox> getActiveHitBoxes() const override;
    void takeDamage(float health_damage, float posture_damage) override;

private:
    Combo combo;

    const float BODY_RADIUS = 0.75f;
    const float ATTACK_REACH = 1.2f;
    const float ATTACK_RADIUS = 0.8f;

    void updateAI(float dt, const Vector3& player_position);
};