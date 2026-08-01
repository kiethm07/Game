#pragma once
#include <raylib.h>
#include <raymath.h>

/// How the camera frames the action. One shot today; the deathblow side view
/// will be a second, and is the reason this is an enum rather than nothing.
enum class CameraShot { Follow };

/// How much room the action needs. A reading of what the player is doing, not
/// a copy of their state: the camera has no opinion about gaits, only about how
/// far back it has to stand.
enum class CameraFraming { Close, Wide };

/// Everything the camera reads each tick that it does not own itself.
///
/// One struct rather than a growing parameter list, for the reason
/// UpdateContext and ActionGate are: what the camera depends on is then visible
/// in one place, and the caller decides it rather than the camera reaching back
/// for it. The framing in particular is a decision about the world — a finisher
/// will one day widen it from facts the camera has no access to.
struct CameraFrame {
    Vector3 target = { 0.0f, 0.0f, 0.0f }; ///< What the camera is framing.
    Vector2 look = { 0.0f, 0.0f };         ///< Raw mouse delta.
    float dt = 0.0f;
    CameraFraming framing = CameraFraming::Close;
    CameraShot shot = CameraShot::Follow;
};

class CameraController {
public:
    CameraController();
    ~CameraController() = default;

    // Call this once per frame with what the camera should be looking at and
    // how the action wants to be framed.
    void update(const CameraFrame& frame);

    // Returns the internal Raylib camera for drawing
    Camera3D getCamera() const;
    Vector3 getCameraForward() const;
    Vector3 getCameraRight() const;

private:
    Camera3D camera;

    // Orbital parameters
    /// Current orbit radius, eased toward whatever the frame's framing asks
    /// for. The orbit math reads it every frame, so easing it here is all it
    /// takes to ease the whole shot.
    float distance;
    float pitch; // Up/Down angle
    float yaw;   // Left/Right angle

    // Mouse sensitivity
    float sensitivity;

    /// The two framings, as orbit radii. They bracket the 5.0 the camera used
    /// to sit at, so neither gait reads as a departure from what was there
    /// before — one is a little tighter, the other a little wider.
    static constexpr float CLOSE_DISTANCE = 4.2f;
    static constexpr float WIDE_DISTANCE = 6.2f;

    /// How much of the remaining gap the distance closes per second. At 6.0
    /// that is ~63% in 0.17s and ~95% in 0.5s: quick enough that the push-out
    /// finishes inside a dodge, slow enough that it reads as a move rather than
    /// a cut.
    static constexpr float DISTANCE_DAMPING = 6.0f;
};
