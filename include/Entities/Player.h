#pragma once

#include <Components/CombatComponent.h>
#include <Core/InputManager.h>
#include <Entities/Character.h>

class Player : public Character {
public:
  Player(const InputManager &input_manager);
  ~Player() = default;

  void update(const UpdateContext &ctx) override;
  CharacterRenderData getRenderData() const override;

  void drawHPBar2D() const;

  float getColliderRadius() const override;
  float getColliderHeight() const override;
  std::vector<HurtBox> getHurtBoxes() const override;
  std::vector<HitBox> getActiveHitBoxes() const override;
  void takeDamage(float health_damage, float posture_damage) override;

private:
  const InputManager &input_manager;
  const float MOVEMENT_SPEED = 10.0f;
  const float ROTATION_SPEED = 10.0f;
  const float BODY_HEIGHT = 1.8f;
  const float BODY_RADIUS = 0.5f;
  const float ATTACK_REACH = 1.2f;
  const float ATTACK_RADIUS = 0.8f;

  CombatComponent combat_component;
  Combo combo;

  enum class AnimState {
    IDLE = 10, // UnarmedIdle
    WALK = 42  // UnarmedRunForward
  };

  int currentAnimIndex = static_cast<int>(AnimState::IDLE);
  float animTime = 0.0f; ///< seconds elapsed in the current clip

  Vector3 calculateCameraRelativeDirection(Vector3 camForward,
                                           Vector3 camRight) const;
  void handleCombatAndUtilityInputs();
};