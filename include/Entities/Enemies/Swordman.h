#pragma once

#include <CombatData/Combo.h>
#include <Entities/Enemies/SwordmanAnimator.h>
#include <Entities/Enemy.h>

class Swordman : public Enemy {
public:
  Swordman(Vector3 start_position);
  ~Swordman() override = default;

  void update(const UpdateContext &ctx) override;
  std::vector<HitBox> getActiveHitBoxes() const override;
  CharacterRenderData getRenderData() const override;

protected:
  /// Flinches on a hit that connected. Enemy::takeDamage decides whether one
  /// did and whether the guard caught it; all that is left here is showing it.
  void onDamaged(bool blocked, bool parried) override;

private:
  Combo combo;

  SwordmanAnimator animator;

  std::vector<Vector3> current_path;
  float path_recalc_timer = 0.0f;

  const float BODY_HEIGHT = 1.8f;
  const float BODY_RADIUS = 0.5f;
  const float ATTACK_REACH = 1.2f;
  const float ATTACK_RADIUS = 0.8f;
  const float MOVEMENT_SPEED = 2.0f;
  const float ROTATION_SPEED = 100.0f;
  void setupBehaviorTree();
  const UpdateContext* current_ctx = nullptr;

  Vector3 spawn_position;
  float spawn_yaw = 0.0f;
  float attack_cooldown_timer = 0.0f;
  float move_cooldown_timer = 0.0f;
  float investigation_timer = 0.0f;
  float circle_direction = 1.0f;
  float circle_timer = 0.0f;
  float preferred_distance_min = 3.5f;
  float preferred_distance_max = 4.5f;
};