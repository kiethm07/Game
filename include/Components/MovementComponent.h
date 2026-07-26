#pragma once

#include "raylib.h"

/// Turns a desired move direction into a horizontal velocity and eases facing
/// toward it. Pure movement math — no input, physics, or rendering dependencies,
/// so both player (input intent) and enemy (AI intent) can drive it. Never writes
/// position; PhysicsManager remains the sole authority over position.
class MovementComponent {
public:
  MovementComponent() = default;
  ~MovementComponent() = default;

  /// @param moveDir  Normalized world-space direction (y ignored). Zero = no input.
  /// @param yaw      Current facing yaw in degrees; eased toward moveDir in place.
  /// @param dt       Frame delta time in seconds.
  /// @return Desired horizontal velocity (x,z); zero when moveDir is zero.
  Vector3 resolve(Vector3 moveDir, float &yaw, float dt) const;

private:
  const float MOVEMENT_SPEED = 20.0f;
  const float ROTATION_SPEED = 10.0f;
};
