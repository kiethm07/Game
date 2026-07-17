#pragma once

#include <Core/InputManager.h>
#include <Entities/Character.h>

class Player : public Character {
public:
  Player(const InputManager &input_manager);
  ~Player() = default;

  void update(float dt) override;
  void update(float dt, Vector3 camForward, Vector3 camRight);
  CharacterRenderData getRenderData() const override;

private:
  const InputManager &input_manager;
  const float MOVEMENT_SPEED = 15.0f;
  const float ROTATION_SPEED = 10.0f;

  Vector3 calculateCameraRelativeDirection(Vector3 camForward,
                                           Vector3 camRight) const;
  void handleCombatAndUtilityInputs();
};