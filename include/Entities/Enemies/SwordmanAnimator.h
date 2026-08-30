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

  /// The same approach, above RUN_SPEED_FACTOR times the walk clip's own
  /// authored speed. Selected by comparing against that clip rather than
  /// against a hardcoded m/s, so a pack whose walk was authored slower does not
  /// silently spend its whole time running.
  Run,

  StrafeForward,
  StrafeBack,
  StrafeLeft,
  StrafeRight,

  /// Airborne. Enemies do not jump, so this is only ever reached by walking off
  /// something; a pack without the clip falls through to the locomotion rungs
  /// the way every other optional state does.
  Fall,

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

  /// The guard, held. Called Guard rather than Parry because the clip is the
  /// same for both halves of it -- the parry window and the block that follows
  /// -- exactly as CombatComponent::isGuarding() treats them.
  Guard,

  /// The guard, carried while moving. Falls back to Guard when a pack has no
  /// such clip, which is what the ashigaru does.
  GuardWalk,

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
    float speed = 0.0f;

    /// Feet on something. Only the Fall rung reads it, and only a pack that
    /// ships a Fall clip can act on it.
    bool grounded = true;
  };

  /// The AssetID is a parameter, not a constant, so a second enemy type that
  /// shares this skeleton can reuse this animator with its own model instead
  /// of copying the whole clip table to change one line.
  ///
  /// It now also picks the clip table. Every asset here shares the Mixamo
  /// skeleton, but no longer the clip NAMES: the ashigaru animates off the
  /// player's pack, while the miniboss has a greatsword pack of its own with
  /// its own names (see tools/build_miniboss_pack.py). descTable() switches on
  /// the id; a model with a different rig would still need more than that.
  explicit SwordmanAnimator(AssetID asset = AssetID::ENEMY_ASHIGARU);

  /// Which model this animator poses -- what getRenderData should return,
  /// rather than a literal repeated at the call site.
  AssetID assetId() const { return asset_id; }

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
  AssetID asset_id;
  /// One row per SwordmanAnimState, in enum order, for the pack `asset` carries.
  static const Machine::Desc *descTable(AssetID asset);

  /// How much faster than the walk clip's authored speed the character has to
  /// be moving before Run is chosen over Walk.
  static constexpr float RUN_SPEED_FACTOR = 1.6f;

  /// The single prioritised ladder: the one place that answers "which clip".
  Machine::Selection resolve(const Frame &frame) const;

  Machine anim;

  float locomotion_speed = 0.0f;

  /// Where to enter the Death clip when the body is ALREADY bowed -- a
  /// deathblow taken on a broken posture, rather than a plain death from full
  /// health. 0 keeps the clip's own start.
  ///
  /// Measured, not guessed: the mini boss's PostureBreak holds hips at 0.648,
  /// and the closest whole-skeleton frame in its 2.43 s Death clip is f65
  /// (1.08 s), which brings the mean joint gap the transition has to cover down
  /// from 0.607 m to 0.244 m. The ashigaru is left at 0 because the same
  /// measurement says its Death clip never passes near its FallToKneel pose
  /// -- best frame 0.575 m against frame 0's 0.577 m, which is no improvement
  /// worth cutting a second off the clip for.
  float death_from_bow_start = 0.0f;

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
