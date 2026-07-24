#pragma once

#include <Core/InputManager.h>
#include <Entities/Character.h>

class Player : public Character {
public:
  Player(const InputManager &input_manager);
  ~Player() = default;

  void update(const UpdateContext &ctx) override;
  CharacterRenderData getRenderData() const override;

private:
  const InputManager &input_manager;
  const float MOVEMENT_SPEED = 20.0f;
  const float ROTATION_SPEED = 10.0f;

  enum class AnimState { IDLE = 10, WALK = 42 };

  int currentAnimIndex = static_cast<int>(AnimState::IDLE);
  float animTime = 0.0f; ///< seconds elapsed in the current clip

  Vector3 calculateCameraRelativeDirection(Vector3 camForward,
                                           Vector3 camRight) const;
  void handleCombatAndUtilityInputs();
};