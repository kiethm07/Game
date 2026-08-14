#pragma once

#include <Animation/AnimStateMachine.h>
#include <Components/CombatComponent.h>

/// What a swordman is doing, as far as the animation layer is concerned.
///
/// The same shape as PlayerAnimState and for the same reason — clip, loop flag,
/// playback rate and fade length are decided once, in one prioritised ladder —
/// but a fraction of the rungs: an enemy has no jump, no dodge and no crouch, so
/// the states it does not have are simply absent rather than present and inert.
enum class SwordmanAnimState {
  Idle,

  /// The approach, selected whenever the behavior tree's chase has written a
  /// horizontal velocity.
  Walk,

  StrafeForward,
  StrafeBack,
  StrafeLeft,
  StrafeRight,

  /// The flinch, split the way the player's is: a hit that landed on a raised
  /// guard is a different animation from one that got through. Enemies never
  /// raise a guard today, so only HitReact is reachable — but Enemy::takeDamage
  /// already distinguishes the two cases, so the animator does too rather than
  /// having to grow the distinction back later.
  GuardImpact,
  HitReact,

  Attack,

  /// Guard broken: the deathblow window. Outranks the swing and the flinch
  /// because the break is what the player is being told about — the hit that
  /// caused it queues a flinch on the same frame, and showing that instead
  /// would hide the one pose the whole mechanic hangs on.
  PostureBreak,

  Death,

  Parry,

  Count
};

/// Chooses and drives a swordman's animation.
///
/// Draws from the player's asset, so every clip named in its table is one of the
/// player's — same skeleton, same names (see the manifest in GameRenderer.cpp).
/// Nothing here is root-driven: an enemy's travel is not taken from its clips,
/// so no state claims it is. The renderer still cancels each clip's authored
/// travel, which is what keeps the mesh sitting on the capsule.
class SwordmanAnimator {
public:
  using Machine = AnimStateMachine<SwordmanAnimState>;

  /// Everything the ladder reads that the animator does not own itself, passed
  /// as one struct so the animator never reaches back into the Swordman for it.
  struct Frame {
    const CombatComponent *combat = nullptr;
    const AssetManager *assets = nullptr;
    bool moving = false;

    /// Death is a state of the character, not of the combat machine — it
    /// outranks every other rung and is never left.
    bool dead = false;

    bool strafing = false;
    Vector3 localMoveDir = {0.0f, 0.0f, 0.0f};
  };

  SwordmanAnimator();

  /// Resolves clip names to indices. True on the frame it does so, which is
  /// when locomotionSpeed() becomes meaningful.
  bool resolveClips(const AssetManager &assets);

  /// The speed the character would have to travel to keep the walk clip's feet
  /// planted, or 0 when that clip has no authored travel. Nothing consumes it
  /// yet; it is what an AI that chases should move at.
  float locomotionSpeed() const { return locomotion_speed; }

  /// Queues a flinch for the next updateFlinch(). Queued rather than started on
  /// the spot for the same reason the player's is: damage is dealt from
  /// CombatManager's pass, which runs outside the entity's own update, and the
  /// clip's length is only reachable through an AssetManager the caller may not
  /// have.
  void queueReaction(bool blocked);

  /// Starts whatever flinch queueReaction() left, and ages the running one.
  void updateFlinch(float dt, const AssetManager *assets);

  /// Returns true if the enemy is currently staggered/flinching.
  bool isFlinching() const { return reaction_timer > 0.0f; }

  /// Runs the ladder for this frame and drives the clock from what it picked.
  void update(const Frame &frame, float dt);

  AnimationState renderState() const { return anim.renderState(); }

private:
  /// One row per SwordmanAnimState, in enum order.
  static const Machine::Desc *descTable();

  /// The single prioritised ladder: the one place that answers "which clip".
  Machine::Selection resolve(const Frame &frame) const;

  Machine anim;

  float locomotion_speed = 0.0f;

  /// The flinch queueReaction() left for the next updateFlinch() to start, or
  /// Count for none.
  SwordmanAnimState queued_reaction = SwordmanAnimState::Count;

  /// Which flinch is playing. Meaningful only while the timer runs.
  SwordmanAnimState reaction_state = SwordmanAnimState::HitReact;

  /// Seconds left of the reaction, seeded from the flinch clip's own length so
  /// the reaction ends exactly when the animation does.
  float reaction_timer = 0.0f;

  /// Bumped once per flinch and used as the reaction's variant, so a second hit
  /// arriving during the first one's clip reads as two flinches rather than one
  /// long one.
  unsigned reaction_id = 0;
};
