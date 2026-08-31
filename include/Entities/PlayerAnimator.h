#pragma once

#include <Animation/AnimStateMachine.h>
#include <Components/CombatComponent.h>
#include <Entities/PlayerLocomotion.h>

/// What the player is doing, as far as the animation layer is concerned.
///
/// Derived every frame from CombatState, the physics grounded flag and the
/// landing and flinch timers — never stored as authority. Gameplay questions
/// ("may I move?", "is my hitbox live?") are still CombatState's and
/// ActionGate's to answer; this enum exists so that clip choice, loop flag,
/// playback rate and root-motion ownership are decided exactly once, in one
/// prioritised ladder, instead of being re-derived from a handful of booleans at
/// each use site.
enum class PlayerAnimState {
  Idle,

  /// The two gaits, one clip each. Which one plays is Gait's to say, not this
  /// ladder's — the gate decides the gait and the speed together, and both
  /// arrive in the frame. Each is time-scaled to the speed that came with it,
  /// so neither slides its feet at whatever fraction of full speed a crouch or
  /// a guard has left it travelling at.
  Walk,
  Run,

  StrafeForward,
  StrafeBack,
  StrafeLeft,
  StrafeRight,

  /// The two halves of being airborne. Jump is the launch and the rise; Fall
  /// takes over past the apex, and is also what a step off a ledge shows
  /// straight away since it never rises at all.
  Jump,
  Fall,

  /// Touchdown recovery. Played on its own timer rather than while a state
  /// lasts, because nothing about the gameplay state changes on landing — only
  /// what the character looks like.
  Land,

  GuardImpact,

  /// The guard held while walking. A separate state rather than the Guard clip
  /// with movement layered on: the block stance turns the pelvis away from the
  /// direction of travel, so a stride cannot be blended onto it locally — the
  /// pack ships the combination as its own authored cycle.
  GuardWalk,
  Guard,

  /// One per authored dodge clip. Kept as four states rather than one state
  /// with four clips so that each keeps its own row in the table: the backstep
  /// is authored half again as long as the others and needs its own rate.
  /// Which one plays is chosen from the movement input at the moment the dodge
  /// starts, and held for the dodge's duration.
  DodgeForward,
  DodgeBack,
  DodgeLeft,
  DodgeRight,

  Attack,
  HitReact,

  /// The two states that end an action rather than being one. Both name a clip
  /// the asset ships, and both rungs are still guarded on clipFor() >= 0 like
  /// every other — an asset without them falls through to the locomotion states
  /// rather than freezing.
  PostureBreak,
  Death,

  Count
};

/// Chooses and drives the player's animation.
///
/// Owns the one prioritised ladder that answers "which clip", the table
/// describing each state, and the flinch timer — which lives here rather than
/// with the gameplay state because a flinch gates nothing; it is purely
/// something the character is seen to do.
class PlayerAnimator {
public:
  using Machine = AnimStateMachine<PlayerAnimState>;

  /// Everything the ladder reads that the animator does not own itself. Passed
  /// as one struct so the animator never reaches back into the Player for it,
  /// and so what the ladder depends on is visible in one place.
  struct Frame {
    const CombatComponent *combat = nullptr;
    const AssetManager *assets = nullptr;
    bool grounded = true;
    float verticalVelocity = 0.0f;
    bool moving = false;

    /// From PlayerLocomotion.
    bool staggered = false;
    bool landingVisible = false;
    unsigned landId = 0;
    Stance stance = Stance::Standing;

    /// The gate's two answers about travel: which cycle the player is in, and
    /// what fraction of full speed they are covering ground at. Taken together
    /// they are what time-scales the cycle, so the clip that plays and the
    /// distance covered are two readings of one decision rather than two
    /// decisions that have to be kept in agreement.
    Gait gait = Gait::Walking;
    float speedScale = 1.0f;

    bool lockedOn = false;
    Vector3 localMoveDir = {0.0f, 0.0f, 0.0f};

    /// Out of health. Sits above every other field in the ladder rather than
    /// alongside them: a corpse does not stride, swing or flinch, so this is
    /// read before anything else and nothing below it can override it.
    bool dead = false;
  };

  /// What the frame's animation implies for movement. The track is never null;
  /// an entity with no assets gets an empty one.
  struct Result {
    const RootMotion::Track *track = nullptr;
    bool rootDriven = false;
  };

  PlayerAnimator();

  /// Resolves clip names to indices. True on the frame it does so, which is
  /// when locomotionSpeed() becomes meaningful.
  bool resolveClips(const AssetManager &assets);

  /// The character's full speed: what they travel at with the gate imposing no
  /// scale, which is the run clip's authored speed taken at RUN_SPEED_SCALE. 0
  /// when the run clip has no authored travel. Every other speed in the game is
  /// this one times an ActionGate::moveSpeedScale, which is also what lets a
  /// single rule time-scale every cycle.
  float locomotionSpeed() const { return locomotion_speed; }

  /// The landing clip's playable length, less the lead-in the state skips, so a
  /// caller timing the recovery against it runs out with the clip rather than
  /// holding a finished pose for the length of a descent already flown.
  float landPlayDuration(const AssetManager *assets) const;

  /// The death clip's playable length. 0 when the asset does not contain it,
  /// which is the honest answer for a caller timing a wait against it: with no
  /// clip there is no fall to watch, so there is nothing to wait for.
  ///
  /// The row plays at rate 1.0, so the clip's own length is also how long it is
  /// on screen — no division here, unlike dodgeDuration().
  float deathDuration(const AssetManager &assets) const;

  /// Starts whatever flinch queueReaction() left, and ages the running one.
  void updateFlinch(float dt, const AssetManager *assets);

  /// Returns true if the player is currently flinching / in hit reaction.
  bool isFlinching() const {
    if (reaction_timer > 0.0f) {
      return true;
    }
    if (queued_reaction != PlayerAnimState::Count) {
      return true;
    }
    return false;
  }

  /// Queues a flinch for the next updateFlinch(). Queued rather than started on
  /// the spot because damage is dealt from CombatManager's pass, which runs
  /// outside the player's own update: the animation clock is only ever advanced
  /// from update(), and the clip's length is only reachable through an
  /// AssetManager the caller may not have.
  void queueReaction(bool blocked);

  /// Which dodge clip a world-space input direction calls for, resolved in the
  /// character's own frame. A directionless input backsteps.
  PlayerAnimState dodgeStateFor(Vector3 worldDirection, float yawDeg) const;

  /// How long that dodge lasts: the clip's own length, divided by the playback
  /// rate its row gives it, because a time-scaled clip finishes proportionally
  /// sooner and the commitment has to end with it. 0 when the clip is missing.
  float dodgeDuration(const AssetManager &assets, PlayerAnimState dodge) const;

  /// Latches the dodge being played. Held for the dodge's whole length:
  /// re-reading the input every frame would let the clip swap mid-roll, and
  /// would fight the root motion, which is committed travel in the direction
  /// the clip was authored for.
  void setDodge(PlayerAnimState dodge) { dodge_state = dodge; }

  /// Runs the ladder for this frame and drives the clock from what it picked.
  Result update(const Frame &frame, float dt);

  /// This frame's root translation in the model's local space, for a caller
  /// that means to turn it into movement.
  Vector3 sampleRootDelta(const RootMotion::Track &track) const {
    return anim.sampleRootDelta(track);
  }

  AnimationState renderState() const { return anim.renderState(); }

private:
  /// Where the landing clip's feet reach the floor, in seconds. The clip is
  /// authored as a fall *into* a landing: it opens with the toes a full unit
  /// above the ground and flies them down over its first 18 frames, which the
  /// engine has meanwhile flown for real. Playing that lead-in on touchdown
  /// would sink the character through the floor they just landed on, so the
  /// state enters the clip here instead — measured off the toe bones, which
  /// first read zero at frame 16 of 60.
  static constexpr float LAND_CONTACT = 0.30f;

  /// How fast the player travels relative to the run clip's authored speed.
  /// 1.0 plays the clip at its natural pace; higher time-scales the clip to
  /// match so the feet keep up instead of sliding.
  static constexpr float RUN_SPEED_SCALE = 2.0f;

  /// One row per PlayerAnimState, in enum order. A member so the private
  /// constants above are in scope for it.
  static const Machine::Desc *descTable();

  /// Whether a state already has the guard up on screen. The three that do are
  /// the raise-and-hold, the guarded walk and the blocked flinch — a flinch
  /// that got through the guard is not one of them, because an unblocked hit
  /// knocks the character out of the pose and the raise back into it is then
  /// the truthful thing to show.
  static bool isGuardPose(PlayerAnimState state);

  /// The single prioritised ladder: the one place that answers "which clip".
  Machine::Selection resolve(const Frame &frame) const;

  /// Playback rate for a travelling cycle: the speed the player is actually
  /// covering ground at, over the speed the cycle was authored to cover it at.
  /// One rule for the walk, the run and the guarded walk alike — a cycle whose
  /// feet are planted is one played at the ratio between those two numbers, and
  /// the three differ only in which clip and which gate scale they hand it.
  ///
  /// Falls back to the row's own rate when there is nothing to measure: no
  /// assets, or a cycle with no authored travel.
  float cycleRate(const Frame &frame, PlayerAnimState cycle) const;

  Machine anim;

  float locomotion_speed = 0.0f;

  /// Where the guard clip's raise has finished and the held pose begins, in
  /// seconds — its playable end, since the clip is authored as a raise that
  /// finishes guard-up and holds there. Entering the clip here is how returning
  /// to a guard that was never dropped resumes the pose instead of raising the
  /// sword a second time. Zero until the clip is resolved, which is also the
  /// harmless answer when the asset does not contain it.
  float guard_hold_at = 0.0f;

  /// The flinch queueReaction() left for the next updateFlinch() to start, or
  /// Count for none.
  PlayerAnimState queued_reaction = PlayerAnimState::Count;

  /// Which flinch is playing — GuardImpact for a hit that landed on a raised
  /// guard, HitReact for one that did not. The two are the same machinery and
  /// share this one slot, so a hit that connects during a flinch replaces it
  /// rather than queueing behind it. Meaningful only while the timer runs.
  PlayerAnimState reaction_state = PlayerAnimState::HitReact;

  /// Seconds left of the reaction. Seeded from the flinch clip's own length, so
  /// the reaction ends exactly when the animation does.
  float reaction_timer = 0.0f;

  /// Bumped once per flinch, and used as the reaction selection's variant. A
  /// second hit arrives while the impact clip is already playing, so without
  /// this it would read as one long flinch rather than two.
  unsigned reaction_id = 0;

  PlayerAnimState dodge_state = PlayerAnimState::DodgeBack;
};
