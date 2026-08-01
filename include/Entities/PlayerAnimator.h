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
  Run,

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

  /// Authored clips do not exist yet: these resolve to index -1, which makes
  /// their rungs of the ladder inert. Wiring one up is a clip name in the
  /// table plus the condition that selects it.
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

  /// The speed the character should travel to keep the run clip's feet planted,
  /// or 0 when the run clip has no authored travel. Only the controller's speed
  /// comes from here; the matching clip time-scale is the Run row's rate.
  float locomotionSpeed() const { return locomotion_speed; }

  /// The landing clip's playable length, less the lead-in the state skips, so a
  /// caller timing the recovery against it runs out with the clip rather than
  /// holding a finished pose for the length of a descent already flown.
  float landPlayDuration(const AssetManager *assets) const;

  /// Starts whatever flinch queueReaction() left, and ages the running one.
  void updateFlinch(float dt, const AssetManager *assets);

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
  static constexpr float RUN_SPEED_SCALE = 1.6f;

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

  Machine anim;

  float locomotion_speed = 0.0f;

  /// Where the guard clip's raise has finished and the held pose begins, in
  /// seconds — its playable end, since the clip is authored as a raise that
  /// finishes guard-up and holds there. Entering the clip here is how returning
  /// to a guard that was never dropped resumes the pose instead of raising the
  /// sword a second time. Zero until the clip is resolved, which is also the
  /// harmless answer when the asset does not contain it.
  float guard_hold_at = 0.0f;

  /// Playback rate for the guard-walk cycle: the speed a guarding player
  /// actually travels at, over the speed the cycle was authored to travel at,
  /// so its feet stay planted the way the run's do. Resolved once from the two
  /// clips rather than written into the table, because neither number is known
  /// until the model is loaded. Left at the row's own rate when the cycle has
  /// no authored travel to measure.
  float guard_walk_rate = 1.0f;

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
