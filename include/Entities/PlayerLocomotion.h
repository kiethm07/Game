#pragma once

#include <Components/CombatComponent.h>

/// The player's posture. Distinct from CombatState, which describes what they
/// are doing with their weapon; this describes how they are carrying
/// themselves, which the combat machine has no opinion about but which changes
/// what they may do.
enum class Stance { Standing, Crouching };

/// What the player may start this instant, and how fast they travel if they
/// move.
///
/// The point of gathering these into one struct is that every restriction is
/// then expressed in exactly one function — PlayerLocomotion::gate() — instead
/// of as a condition sprinkled through the input handler. Adding a rule is a
/// line there; asking whether a rule holds is reading one field.
struct ActionGate {
  bool canMove = true;
  bool canAttack = true;
  bool canDodge = true;
  bool canJump = true;
  bool canGuard = true;

  /// Multiplies the velocity the movement component resolves. 1.0 is the
  /// character's full speed, which is the run clip's authored speed.
  float moveSpeedScale = 1.0f;
};

/// The player state that is neither combat nor animation: which stance they are
/// in, and the landing they are recovering from.
///
/// Landing lives here rather than in the animator because a stagger is a
/// gameplay fact — it decides what the player may do — even though its length
/// comes from a clip. The animator supplies that length and reads the result;
/// it does not own it.
class PlayerLocomotion {
public:
  /// How fast a guarding player walks, as a fraction of full speed. A raised
  /// guard is a commitment to defence: it should cost mobility, but not so much
  /// that closing or retreating behind it stops being an option.
  ///
  /// Public because the animator needs it too: the guard-walk cycle is
  /// time-scaled to the speed this imposes, and a second copy of the number
  /// would let the stride and the travel drift apart.
  static constexpr float BLOCK_SPEED_SCALE = 0.5f;

  /// Ages the timers and watches for touchdown. Returns true on the single
  /// frame a stagger begins, which is the caller's cue to cancel whatever
  /// combat action was running: an attack whose root motion carried the player
  /// off a ledge must not keep a live hitbox through a landing they no longer
  /// control.
  ///
  /// `landPlayDuration` is the landing clip's playable length as the animator
  /// measures it. Passed in rather than stored so the clip stays the source of
  /// truth for how long the recovery reads.
  bool update(float dt, bool grounded, float verticalVelocity,
              float landPlayDuration);

  /// The whole rule set, in priority order.
  ActionGate gate(const CombatComponent &combat, bool grounded) const;

  /// Rule: a jump taken from a crouch stands up first rather than launching
  /// crouched. A transition rather than a permission, so it is applied at the
  /// moment of the jump instead of being expressed in gate().
  void standUp() { stance = Stance::Standing; }

  void setStance(Stance s) { stance = s; }
  Stance getStance() const { return stance; }

  /// True while a landing is holding the player. A committed state in the same
  /// sense an attack or a dodge is, just one the combat state machine has no
  /// opinion about — landing is not a combat action.
  bool isStaggered() const { return stagger_timer > 0.0f; }

  /// True while the landing clip may still be shown. Outlives the stagger, so a
  /// player who stands still sees the recovery finish while one who wants to
  /// move gets control back long before it does.
  bool isLandingVisible() const { return land_display_timer > 0.0f; }

  /// Bumped once per landing, so that landing twice in quick succession replays
  /// the clip instead of holding the last pose of the first one.
  unsigned landId() const { return land_id; }

private:
  Stance stance = Stance::Standing;

  /// Fastest descent seen during the current time in the air. PhysicsManager
  /// zeroes vertical velocity as it snaps the character to the ground, so by
  /// the time the landing is observed the speed that would judge its severity
  /// is already gone; this captures it while still falling.
  float fall_speed = 0.0f;

  /// Last frame's grounded flag, so that the frame of touchdown can be told
  /// apart from every frame after it.
  bool was_grounded = true;

  /// Seconds left of the landing's hold on the player, seeded from
  /// LAND_LOCK_DURATION.
  float stagger_timer = 0.0f;

  /// Seconds left of the landing clip, seeded from its own playable length.
  /// Kept apart from the stagger because the two answer different questions:
  /// how long the player is held, and how long the clip may be shown.
  float land_display_timer = 0.0f;

  unsigned land_id = 0;

  /// Descent speed, in units/second, above which a touchdown staggers. A jump
  /// taken at the player's jump speed comes back down at that same speed, so
  /// every real jump lands; a 0.3-unit step down reaches only ~2.4, so walking
  /// off a kerb does not punctuate every stride with a crouch.
  static constexpr float LAND_TRIGGER_SPEED = 3.5f;

  /// How long a landing holds the player, in seconds, measured from the moment
  /// the feet touch. The clip has 0.78s of authored recovery after contact, but
  /// locking control for all of it turns every jump into a commitment; this
  /// covers the impact and the deepest part of the crouch, which is the
  /// readable part, and releases the player on the way back up.
  static constexpr float LAND_LOCK_DURATION = 0.35f;

  /// How fast a crouching player moves. Slow enough to read as deliberate,
  /// which is what a stance held for stealth has to be.
  static constexpr float CROUCH_SPEED_SCALE = 0.5f;
};
