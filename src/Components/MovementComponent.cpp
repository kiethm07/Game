#include <Components/MovementComponent.h>
#include <cmath>

Vector3 MovementComponent::resolve(Vector3 moveDir, float &yaw, float dt) const {
  const bool isMoving = (moveDir.x != 0.0f || moveDir.z != 0.0f);
  if (!isMoving) {
    return {0.0f, 0.0f, 0.0f};
  }

  // Ease facing toward the movement direction.
  // Target yaw from the horizontal direction vector.
  float target_yaw = std::atan2(moveDir.x, moveDir.z) * RAD2DEG;

  // Shortest angular distance.
  float angle_diff = target_yaw - yaw;
  while (angle_diff < -180.0f)
    angle_diff += 360.0f;
  while (angle_diff > 180.0f)
    angle_diff -= 360.0f;

  // Safeguard the interpolation factor against dt spikes so it never overshoots.
  float alpha = ROTATION_SPEED * dt;
  if (alpha > 1.0f)
    alpha = 1.0f;

  yaw += angle_diff * alpha;

  // Keep yaw cleanly wrapped to 0..360 to avoid float drift over time.
  while (yaw < 0.0f)
    yaw += 360.0f;
  while (yaw >= 360.0f)
    yaw -= 360.0f;

  return {moveDir.x * MOVEMENT_SPEED, 0.0f, moveDir.z * MOVEMENT_SPEED};
}
