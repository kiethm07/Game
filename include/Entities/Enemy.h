#pragma once

#include <Core/InputManager.h>
#include <Entities/Character.h>

class Enemy : public Character {
public:
  Enemy();
  ~Enemy() = default;

  void update(const UpdateContext &ctx) override;
  CharacterRenderData getRenderData() const override;

private:
  const float MOVEMENT_SPEED = 5.0f;
  const float ROTATION_SPEED = 100.0f;
  int moveState = 0;

  enum class AnimState {
    IDLE = 6, // UnarmedIdle
    WALK = 23 // UnarmedRunForward
  };

  int   currentAnimIndex = static_cast<int>(AnimState::IDLE);
  float animTime         = 0.0f; ///< seconds elapsed in the current clip

  Vector3 calculateCameraRelativeDirection(Vector3 camForward,
                                           Vector3 camRight) const;
  void handleCombatAndUtilityInputs();
};