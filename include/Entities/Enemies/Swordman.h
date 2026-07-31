#pragma once

#include <Animation/AnimStateMachine.h>
#include <CombatData/Combo.h>
#include <Entities/Enemy.h>

/// The one clip the ashigaru model carries. A single-entry ladder, but it goes
/// through the same machinery the player's does, so the clock is advanced and
/// the render state is filled the same way rather than by hand.
enum class SwordmanAnimState { Idle, Count };

class Swordman : public Enemy {
public:
  Swordman(Vector3 start_position);
  ~Swordman() override = default;

  void update(const UpdateContext &ctx) override;
  std::vector<HitBox> getActiveHitBoxes() const override;
  CharacterRenderData getRenderData() const override;

private:
  Combo combo;

  AnimStateMachine<SwordmanAnimState> anim;

  const float BODY_HEIGHT = 1.8f;
  const float BODY_RADIUS = 0.5f;
  const float ATTACK_REACH = 1.2f;
  const float ATTACK_RADIUS = 0.8f;
  const float MOVEMENT_SPEED = 5.0f;
  const float ROTATION_SPEED = 100.0f;
  void setupBehaviorTree();
  const UpdateContext* current_ctx = nullptr;
};