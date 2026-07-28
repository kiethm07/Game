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

  /// Sets travel speed in world units per second. Owners derive this from the
  /// run clip's authored speed (RootMotion::Track::authoredSpeed) so the stride
  /// matches the ground travel; a hardcoded value slides the feet as soon as it
  /// disagrees with the animation.
  void setSpeed(float speed) { movement_speed = speed; }
  float getSpeed() const { return movement_speed; }

private:
  /// Fallback only, for owners that never call setSpeed. Roughly the authored
  /// speed of a Mixamo run cycle.
  float movement_speed = 4.0f;
  const float ROTATION_SPEED = 10.0f;
};
