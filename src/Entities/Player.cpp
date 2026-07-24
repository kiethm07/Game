#include <Entities/Player.h>
#include <cmath>
#include <raymath.h>

Player::Player(const InputManager &input_manager)
    : input_manager(input_manager) {
  position = {0, 0, 0};
}

void Player::update(const UpdateContext &ctx) {
  const float dt = ctx.dt;
  // Advance framerate-independent playback time.
  animTime += dt;

  Vector3 moveDirection =
      calculateCameraRelativeDirection(ctx.camForward, ctx.camRight);

  bool isMoving = (moveDirection.x != 0.0f || moveDirection.z != 0.0f);
  int targetAnimIndex = isMoving ? static_cast<int>(Player::AnimState::WALK) : static_cast<int>(Player::AnimState::IDLE);

  if (currentAnimIndex != targetAnimIndex) {
      currentAnimIndex = targetAnimIndex;
      animTime = 0.0f; // Reset animation when transitioning
  }

  if (isMoving) {
    // Apply position displacement
    position.x += moveDirection.x * MOVEMENT_SPEED * dt;
    position.z += moveDirection.z * MOVEMENT_SPEED * dt;

    // Calculate the target angle based on the horizontal direction vector
    float target_yaw = std::atan2(moveDirection.x, moveDirection.z) * RAD2DEG;

    // Calculate the shortest angular distance to prevent erratic 360-degree
    // spinning
    float angle_diff = target_yaw - rotation.y;
    while (angle_diff < -180.0f)
      angle_diff += 360.0f;
    while (angle_diff > 180.0f)
      angle_diff -= 360.0f;

    // Fluidly interpolate rotation alongside camera and input vector updates
    rotation.y += angle_diff * ROTATION_SPEED * dt;
  }
}

CharacterRenderData Player::getRenderData() const {
  return {
      AssetID::PLAYER_WOLF,
      /*transform*/ { position, rotation, {1.0f, 1.0f, 1.0f} },
      /*animation*/ { currentAnimIndex, animTime }
  };
}

Vector3 Player::calculateCameraRelativeDirection(Vector3 camForward,
                                                 Vector3 camRight) const {
  Vector3 direction = {0.0f, 0.0f, 0.0f};
  if (input_manager.isActionHeld(GameAction::MOVE_FORWARD))
    direction = Vector3Add(direction, camForward);
  if (input_manager.isActionHeld(GameAction::MOVE_BACKWARD))
    direction = Vector3Subtract(direction, camForward);
  if (input_manager.isActionHeld(GameAction::MOVE_RIGHT))
    direction = Vector3Add(direction, camRight);
  if (input_manager.isActionHeld(GameAction::MOVE_LEFT))
    direction = Vector3Subtract(direction, camRight);

  if (direction.x != 0.0f || direction.z != 0.0f) {
    direction = Vector3Normalize(direction);
  }
  return direction;
}

void Player::handleCombatAndUtilityInputs() {
  if (input_manager.isActionPressed(GameAction::ATTACK)) {
  }
  if (input_manager.isActionPressed(GameAction::DEFLECT)) {
  }
  if (input_manager.isActionPressed(GameAction::DODGE)) {
  }
  if (input_manager.isActionPressed(GameAction::LOCK_ON)) {
  }
}