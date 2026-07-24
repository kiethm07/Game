#pragma once

#include <CombatData/Combo.h>
#include <Entities/Enemy.h>

class Swordman : public Enemy {
public:
  Swordman(Vector3 start_position);
  ~Swordman() override = default;

  void update(const UpdateContext &ctx) override;
  std::vector<HitBox> getActiveHitBoxes() const override;
  CharacterRenderData getRenderData() const override;

private:
  Combo combo;

  const float BODY_HEIGHT = 1.8f;
  const float BODY_RADIUS = 0.5f;
  const float ATTACK_REACH = 1.2f;
  const float ATTACK_RADIUS = 0.8f;
  const float MOVEMENT_SPEED = 5.0f;
  const float ROTATION_SPEED = 100.0f;

  void updateAI(float dt, const Vector3 &player_position);
};