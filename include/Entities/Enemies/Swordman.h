#pragma once

#include <Entities/Enemy.h>
#include <CombatData/Combo.h>

class Swordman : public Enemy {
public:
    Swordman(Vector3 start_position);
    ~Swordman() override = default;

    void update(float dt, const Vector3& player_position) override;
    std::vector<HitBox> getActiveHitBoxes() const override;

private:
    Combo combo;

    const float BODY_HEIGHT = 1.8f;
    const float BODY_RADIUS = 0.5f;
    const float ATTACK_REACH = 1.2f;
    const float ATTACK_RADIUS = 0.8f;

    void updateAI(float dt, const Vector3& player_position);
};