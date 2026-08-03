#include <Core/CameraController.h>
#include <math.h>
#include <raymath.h>

CameraController::CameraController() {
  // Initialize Raylib Camera struct
  camera.position = {0.0f, 0.0f, 0.0f};
  camera.target = {0.0f, 0.0f, 0.0f};
  camera.up = {0.0f, 1.0f, 0.0f};
  camera.fovy = 60.0f; // A slightly wider FOV feels better for action games
  camera.projection = CAMERA_PERSPECTIVE;

  // Default orbital settings
  distance = CLOSE_DISTANCE; // The player starts idle, so start framed for it
  pitch = FOLLOW_PITCH;      // Looking slightly down at the player
  yaw = 0.0f;                // Looking straight ahead
  sensitivity = 0.2f;        // Adjust this to make the mouse feel right
}
static float shortestAngleDelta(float from, float to) {
  float delta = to - from;
  while (delta < -180.0f)
    delta += 360.0f;
  while (delta > 180.0f)
    delta -= 360.0f;
  return delta;
}

void CameraController::update(const CameraFrame &frame) {
  const Vector3 target_position = frame.target;

  const bool cinematic = (frame.shot == CameraShot::Deathblow);
  if (cinematic)
    focus_point = frame.focus;

  // Every eased quantity below closes a constant fraction of its remaining
  // gap per second, so the whole shot settles identically at 30fps and at 144
  // — a lerp with a dt-scaled alpha would not, and would need a clamp against
  // dt spikes the way MovementComponent's facing does. This form is bounded
  // in [0, 1) for every dt >= 0 by construction, and a dt of zero leaves
  // everything untouched.
  const float shot_alpha = 1.0f - expf(-SHOT_DAMPING * frame.dt);
  const float unwind_alpha = 1.0f - expf(-DISTANCE_DAMPING * frame.dt);

  // 1. Aim. The mouse steers the Follow orbit; the deathblow takes it away
  // and drives yaw and pitch to a composed heading instead.
  //
  // Control does not come back the instant the shot ends. The shot leaves the
  // camera side-on and pitched steeply down — an angle the player never asked
  // for and would have to undo by hand — so the return swings it back behind
  // them first, on the same constant the distance and the look-at unwind on.
  // The whole release is then one move rather than a camera that snaps to a
  // resting position while its own orbit is still easing.
  if (cinematic) {
    // Side-on to the line between the two characters. Both perpendiculars
    // are equally side-on, so take whichever is nearer the angle the camera
    // is already at — the swing is then never more than a quarter turn, and
    // a kill in front of the player does not whip the shot around behind
    // them to reach an arbitrarily-chosen "left".
    const Vector3 axis = Vector3Subtract(focus_point, target_position);
    if (axis.x != 0.0f || axis.z != 0.0f) {
      const float axis_yaw = atan2f(axis.x, axis.z) * RAD2DEG;
      const float left = shortestAngleDelta(yaw, axis_yaw + 90.0f);
      const float right = shortestAngleDelta(yaw, axis_yaw - 90.0f);
      const float nearer = (fabsf(left) <= fabsf(right)) ? left : right;
      yaw += nearer * shot_alpha;
    }
    pitch += (DEATHBLOW_PITCH - pitch) * shot_alpha;

    // Armed for as long as the shot runs, so it is full the frame the shot
    // lets go however long the shot lasted.
    recenter_timer = RECENTER_DURATION;
  } else if (recenter_timer > 0.0f && fabsf(frame.look.x) <= RECENTER_YIELD &&
             fabsf(frame.look.y) <= RECENTER_YIELD) {
    recenter_timer -= frame.dt;

    // Behind the player is half a turn from the way they face: the orbit
    // places the camera along (sin yaw, cos yaw) from the target, and
    // Character forward is (sin rot.y, cos rot.y) in the same convention,
    // so the camera wants the heading opposite theirs. Through
    // shortestAngleDelta like every other angle here, or a kill that leaves
    // the camera at 350 would take the 340-degree way round to 10.
    const float behind = frame.targetYaw + 180.0f;
    yaw += shortestAngleDelta(yaw, behind) * unwind_alpha;
    pitch += (FOLLOW_PITCH - pitch) * unwind_alpha;
  } else {
    // Either no return is running or the player has reached for the camera,
    // which ends it: whatever the shot did to the angle, being fought for
    // control of it is worse.
    recenter_timer = 0.0f;
    yaw -= frame.look.x * sensitivity;
    pitch += frame.look.y * sensitivity;
  }

  // 2. Clamp the pitch to prevent the camera from flipping upside down
  if (pitch > 89.0f)
    pitch = 89.0f;
  if (pitch < -89.0f)
    pitch = -89.0f;

  // 2.5. Ease the orbit radius toward the one this shot asks for. The
  // deathblow overrides the gait's framing outright — how close the kill
  // wants to be seen has nothing to do with how fast the player was moving
  // when they started it — and pushes in on the stiffer constant, then eases
  // back out on the gentle one once the shot releases.
  const float framedDistance =
      cinematic ? DEATHBLOW_DISTANCE
                : ((frame.framing == CameraFraming::Wide) ? WIDE_DISTANCE
                                                          : CLOSE_DISTANCE);
  distance +=
      (framedDistance - distance) * (cinematic ? shot_alpha : unwind_alpha);

  // 2.75. Slide the look-at point from the player to the midpoint of the
  // pair, so the shot holds both of them rather than putting the victim at
  // the edge of frame. Blended rather than switched: the two points are ~0.6
  // units apart and a hard swap between them reads as a cut. The unwind runs
  // on DISTANCE_DAMPING for the same reason the push-out does.
  shot_blend += ((cinematic ? 1.0f : 0.0f) - shot_blend) *
                (cinematic ? shot_alpha : unwind_alpha);

  const Vector3 pair_midpoint =
      Vector3Scale(Vector3Add(target_position, focus_point), 0.5f);
  const Vector3 framed_target =
      Vector3Lerp(target_position, pair_midpoint, shot_blend);

  // 3. Convert degrees to radians for C++ math functions
  float pitchRad = pitch * DEG2RAD;
  float yawRad = yaw * DEG2RAD;

  // 4. Calculate the Camera's position using Spherical to Cartesian math
  // This orbits the camera around the framed point based on the yaw and pitch
  camera.position.x =
      framed_target.x + distance * cosf(pitchRad) * sinf(yawRad);
  camera.position.y = framed_target.y + distance * sinf(pitchRad);
  camera.position.z =
      framed_target.z + distance * cosf(pitchRad) * cosf(yawRad);

  // camera target at some point, raised by AIM_HEIGHT so the shot is centred on
  // the body
  camera.target = {framed_target.x, framed_target.y + AIM_HEIGHT,
                   framed_target.z};
}

Camera3D CameraController::getCamera() const { return camera; }

Vector3 CameraController::getCameraForward() const {
  // Calculate the forward vector from the camera's target and position
  Vector3 forward = Vector3Subtract(camera.target, camera.position);
  // Normalize the vector to make it a unit vector
  forward = Vector3Normalize(forward);
  return forward;
}

Vector3 CameraController::getCameraRight() const {
  // Calculate the right vector from the camera's forward and up vectors
  Vector3 right = Vector3CrossProduct(getCameraForward(), camera.up);
  // Normalize the vector to make it a unit vector
  right = Vector3Normalize(right);
  return right;
}