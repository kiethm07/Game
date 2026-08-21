#pragma once

#include <CombatData/Combo.h>
#include <Entities/Enemies/SwordmanAnimator.h>
#include <Entities/Enemy.h>

class Swordman : public Enemy {
public:
  Swordman(const EnemySpawn &spawn);
  ~Swordman() override = default;

  void update(const UpdateContext &ctx) override;
  CharacterRenderData getRenderData() const override;

protected:
  /// Flinches on a hit that connected. Enemy::takeDamage decides whether one
  /// did and whether the guard caught it; all that is left here is showing it.
  void onDamaged(bool blocked, bool parried) override;

private:
  Combo combo;

  SwordmanAnimator animator;

  // BODY_HEIGHT, BODY_RADIUS, ATTACK_REACH, ATTACK_RADIUS and ROTATION_SPEED
  // used to sit here and were referenced by nothing -- the collider actually
  // comes from Enemy's body_height/body_radius. They are gone because five
  // dead constants are exactly what a second enemy type would copy and then
  // wonder why editing does nothing.
  const float MOVEMENT_SPEED = 2.0f;
  void setupBehaviorTree();

  // spawn_position and spawn_yaw are Enemy's now, set from the spawn in one
  // place so they cannot disagree with `rotation`.
  float attack_cooldown_timer = 0.0f;
  float move_cooldown_timer = 0.0f;
  float investigation_timer = 0.0f;
};