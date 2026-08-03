#pragma once
#include <raylib.h>
#include <raymath.h>

/// How the camera frames the action.
enum class CameraShot {
    /// The orbit the player steers with the mouse.
    Follow,

    /// The deathblow. Takes the mouse away for the length of the animation and
    /// swings around to look down the side of the two characters, close in.
    /// The shot is composed from the pair, not from the player alone, which is
    /// why CameraFrame carries a second point for it.
    Deathblow
};

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

    /// The shot's second subject — the victim of a deathblow. Read only while
    /// `shot` names a shot that frames a pair; the Follow orbit ignores it, so
    /// leaving it at the origin the rest of the time costs nothing.
    Vector3 focus = { 0.0f, 0.0f, 0.0f };
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
    Vector3 focus_point = { 0.0f, 0.0f, 0.0f };

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

    /// The deathblow orbit: closer than either framing, and nearly level with
    /// the pair rather than looking down on them. The two characters stand 1.2
    /// units apart, so at this radius a 60-degree FOV spans ~3.5 units across
    /// the plane they occupy — both bodies in frame with room either side, and
    /// no danger of clipping into either.
    static constexpr float DEATHBLOW_DISTANCE = 3.0f;
    static constexpr float DEATHBLOW_PITCH = 10.0f;

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
