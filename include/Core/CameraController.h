#pragma once
#include <raylib.h>
#include <raymath.h>
#include <vector>

class PhysicsObstacle;

/// How the camera frames the action.
enum class CameraShot { Follow, Deathblow, LockOn };

enum class CameraFraming { Close, Wide };

struct CameraFrame {
  Vector3 target = {0.0f, 0.0f, 0.0f}; ///< What the camera is framing.
  Vector2 look = {0.0f, 0.0f};         ///< Raw mouse delta.
  float dt = 0.0f;
  CameraFraming framing = CameraFraming::Close;
  CameraShot shot = CameraShot::Follow;

  Vector3 focus = {0.0f, 0.0f, 0.0f};

  /// Which way the subject is facing, in degrees, in the same convention as
  /// Character::rotation.y. Only the return from a shot reads it: "behind the
  /// player" is a fact about the world, and the camera has no other way to know
  /// which way that is.
  float targetYaw = 0.0f;
  
  const std::vector<PhysicsObstacle>* obstacles = nullptr;
};

class CameraController {
public:
  CameraController();
  ~CameraController() = default;

  void update(const CameraFrame &frame);

  // Returns the internal Raylib camera for drawing
  Camera3D getCamera() const;
  Vector3 getCameraForward() const;
  Vector3 getCameraRight() const;

private:
  Camera3D camera;
  float distance;
  float current_distance;
  float pitch; // Up/Down angle
  float yaw;   // Left/Right angle

  // Mouse sensitivity
  float sensitivity;

  /// How far the shot has taken over, 0 (pure Follow) to 1 (fully composed on
  /// the pair). Eases both ways, so the same scalar that swings the camera
  /// onto the kill also walks it back off afterwards — there is no separate
  /// exit path to keep in sync with the entry.
  ///
  /// Only the look-at point needs it: distance, pitch and yaw are already
  /// eased quantities, so they cross over simply by being given a different
  /// destination while the shot runs.
  float shot_blend = 0.0f;

  /// The last second subject the frame supplied. Held rather than re-read
  /// because the unwind outlives the shot: the caller has stopped naming a
  /// victim by then, and the look-at still has to slide back off them.
  Vector3 focus_point = {0.0f, 0.0f, 0.0f};

  /// Seconds left of the swing back behind the player. Armed for as long as a
  /// shot runs, so it starts full the frame the shot lets go.
  ///
  /// A clock rather than a "close enough yet?" test because the target moves:
  /// the player is free again the instant the shot ends, and one walking in a
  /// circle would keep the camera glued behind them forever if arrival were the
  /// only way out.
  float recenter_timer = 0.0f;

  static constexpr float CLOSE_DISTANCE = 4.2f;
  static constexpr float WIDE_DISTANCE = 5.2f;

  // uplift height from the feet of the character
  static constexpr float AIM_HEIGHT = 1.3f;

  static constexpr float DISTANCE_DAMPING = 6.0f;

  static constexpr float DEATHBLOW_DISTANCE = 4.0f;
  static constexpr float DEATHBLOW_PITCH = 40.0f;

  static constexpr float FOLLOW_PITCH = 30.0f;

  // how long the swing back behind the player runs.
  static constexpr float RECENTER_DURATION = 0.6f;

  /// Mouse movement past this, in raw pixels, ends the return early. A player
  /// reaching for the camera outranks it — a swing that fought the mouse would
  /// be worse than the angle it was correcting. Not zero, so a tremor on a
  /// resting hand does not count as reaching for it.
  static constexpr float RECENTER_YIELD = 0.5f;

  /// The shot's own damping, and much stiffer than DISTANCE_DAMPING: ~63% in
  /// 0.07s and ~95% in 0.21s. The execution's startup is 0.55s, so the camera
  /// has arrived and settled well before the blade does — the swing reads as
  /// the shot being taken rather than the camera chasing the action.
  ///
  /// Only the entry uses it. The unwind is deliberately left on the gentler
  /// constants: snapping back the instant the animation ends would undo the
  /// whole effect.
  static constexpr float SHOT_DAMPING = 14.0f;
};
