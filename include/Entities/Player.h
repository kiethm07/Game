#pragma once

#include <Components/AnimationComponent.h>
#include <Components/CombatComponent.h>
#include <Components/MovementComponent.h>
#include <Core/InputManager.h>
#include <Entities/Character.h>
#include <Rendering/RootMotion.h>

/// What the player is doing, as far as the animation layer is concerned.
///
/// Derived every frame from CombatState, the physics grounded flag and the
/// guard-impact timer — never stored as authority. Gameplay questions ("may I
/// move?", "is my hitbox live?") are still CombatState's to answer; this enum
/// exists so that clip choice, loop flag, playback rate and root-motion
/// ownership are decided exactly once, in one prioritised ladder, instead of
/// being re-derived from a handful of booleans at each use site.
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

class Player : public Character {
public:
  Player(const InputManager &input_manager);
  ~Player() = default;

  void update(const UpdateContext &ctx) override;
  CharacterRenderData getRenderData() const override;

  void drawHPBar2D() const;

  float getColliderRadius() const override;
  float getColliderHeight() const override;
  std::vector<HurtBox> getHurtBoxes() const override;
  std::vector<HitBox> getActiveHitBoxes() const override;
  void takeDamage(float health_damage, float posture_damage) override;

private:
  const InputManager &input_manager;

  const float BODY_HEIGHT = 1.8f;
  const float BODY_RADIUS = 0.5f;
  const float ATTACK_REACH = 1.2f;
  const float ATTACK_RADIUS = 0.8f;

  /// Ceiling on root-motion-derived velocity. Root motion divides a frame's
  /// travel by dt, so a dt spike (breakpoint, window drag) would otherwise
  /// produce an unbounded velocity and tunnel the character through geometry.
  static constexpr float MAX_ROOT_MOTION_SPEED = 40.0f;

  /// Take-off speed in units/second. Against PhysicsManager's 9.81 gravity this
  /// apexes at v^2/2g ~= 1.27 units (about two thirds of body height) after
  /// ~0.51s, for a ~1.0s round trip — matching the Jump_2 clip's 1.00s, so the
  /// landing pose reads correctly.
  static constexpr float JUMP_SPEED = 5.0f;

  /// Descent speed, in units/second, above which a touchdown plays the landing
  /// clip. A jump taken at JUMP_SPEED comes back down at that same speed, so
  /// every real jump lands; a 0.3-unit step down reaches only ~2.4, so walking
  /// off a kerb does not punctuate every stride with a crouch.
  static constexpr float LAND_TRIGGER_SPEED = 3.5f;

  /// How long a landing holds the player still, in seconds, measured from the
  /// moment the feet touch. The clip has 0.78s of authored recovery after
  /// contact, but locking control for all of it turns every jump into a
  /// commitment; this covers the impact and the deepest part of the crouch,
  /// which is the readable part, and releases the player on the way back up.
  /// The clip is allowed to finish on its own if they choose to stand still.
  static constexpr float LAND_LOCK_DURATION = 0.35f;

  /// Where the landing clip's feet reach the floor, in seconds. The clip is
  /// authored as a fall *into* a landing: it opens with the toes a full unit
  /// above the ground and flies them down over its first 18 frames, which the
  /// engine has meanwhile flown for real. Playing that lead-in on touchdown
  /// would sink the character through the floor they just landed on, so the
  /// state enters the clip here instead — measured off the toe bones, which
  /// first read zero at frame 16 of 60.
  static constexpr float LAND_CONTACT = 0.30f;

  /// How hard the player may steer while airborne, in units/second^2. Expressed
  /// as an acceleration rather than a blend factor so it stays frame-rate
  /// independent: at roughly 2x ground speed it takes ~0.5s to fully redirect a
  /// jump. Full instant control in the air feels weightless; none at all makes
  /// a mistimed jump unrecoverable.
  static constexpr float AIR_ACCELERATION = 13.0f;

  CombatComponent combat_component;
  MovementComponent movement_component;
  AnimationComponent animation;
  Combo combo;

  /// How fast the player travels relative to the run clip's authored speed.
  /// 1.0 plays the clip at its natural pace; higher time-scales the clip to
  /// match so the feet keep up instead of sliding.
  static constexpr float RUN_SPEED_SCALE = 1.6f;

  /// Everything about a state that does not depend on the current frame. One
  /// row per PlayerAnimState, so adding a state is a table entry plus a rung of
  /// the ladder in resolveAnimState().
  struct AnimDesc {
    /// Looked up by name because clip indices shift with the export toolchain
    /// (see AssetManager::findAnimation). nullptr for states with no authored
    /// animation yet, which leaves their index at -1.
    const char *clip;
    bool loop;
    float rate;
    bool rootDriven;

    /// Seconds spent cross-fading into this state. Per state rather than per
    /// transition because what governs the length is the state being entered:
    /// a swing or a flinch has to land now, a stride can afford to ease.
    float fadeIn;

    /// Seconds to skip at the head of the clip. Zero for all but the landing,
    /// which is authored with its own descent in front of the impact — the
    /// engine has already flown that descent for real, so playing it again
    /// would drop the character through a floor they are standing on.
    float startAt = 0.0f;
  };
  static const AnimDesc &animDesc(PlayerAnimState state);

  /// What resolveAnimState() decided this frame.
  struct AnimSelection {
    PlayerAnimState state = PlayerAnimState::Idle;

    /// Distinguishes re-entering a state from staying in it, for the states
    /// where that difference is visible: a fresh swing and a fresh flinch both
    /// reuse the clip they are already playing, and play() is a no-op on the
    /// current clip. Zero for states that cannot meaningfully re-trigger.
    unsigned variant = 0;

    int clip = -1;
    bool rootDriven = false;
  };

  /// Clip indices per state, resolved by name on the first update. -1 until
  /// then, and for clips the loaded asset does not contain.
  int clip_index[static_cast<int>(PlayerAnimState::Count)];
  bool clips_resolved = false;

  /// Last frame's selection, compared against this frame's to decide whether
  /// the clip needs an explicit rewind.
  AnimSelection prev_anim;

  /// The flinch takeDamage() left for the next update() to start, or Count for
  /// none. Damage is dealt from CombatManager's pass, which runs outside the
  /// player's own update, so the reaction is queued rather than started on the
  /// spot: the animation clock is only ever advanced from update(), and the
  /// clip's length is only reachable through that frame's UpdateContext.
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

  /// Which of the four dodge clips the current dodge is playing. Latched when
  /// the dodge starts and held for its whole length: re-reading the input every
  /// frame would let the clip swap mid-roll, and would fight the root motion,
  /// which is committed travel in the direction the clip was authored for.
  PlayerAnimState dodge_state = PlayerAnimState::DodgeBack;

  /// Seconds left of the landing clip, seeded from its own playable length.
  /// Governs only how long the clip may be shown; the shorter lock below is
  /// what actually holds the player.
  float land_timer = 0.0f;

  /// Seconds left of the landing's hold on the player, seeded from
  /// LAND_LOCK_DURATION. Kept apart from land_timer because the two answer
  /// different questions: the clip outlives the commitment, so that a player
  /// who stands still sees the recovery finish, while one who wants to move
  /// gets control back long before it does.
  float land_lock_timer = 0.0f;

  /// Bumped once per landing, and used as the landing selection's variant, so
  /// that landing twice in quick succession replays the clip instead of holding
  /// the last pose of the first one.
  unsigned land_id = 0;

  /// Fastest descent seen during the current time in the air. PhysicsManager
  /// zeroes vertical velocity as it snaps the character to the ground, so by
  /// the time the landing is observed the speed that would judge its severity
  /// is already gone; this captures it while still falling.
  float fall_speed = 0.0f;

  /// Last frame's grounded flag, so that the frame of touchdown can be told
  /// apart from every frame after it.
  bool was_grounded = true;

  /// True for both halves of a guard — the parry window and the block hold
  /// that follows it. They share one animation, so the two states are treated
  /// as one for clip selection even though only Blocking allows movement.
  bool isGuarding() const;

  /// Starts whatever flinch takeDamage() queued, and ages the running one.
  void updateReaction(const UpdateContext &ctx);

  /// Tracks the fall and starts the landing clip on touchdown.
  void updateLanding(const UpdateContext &ctx);

  /// True while a landing is holding the player in place. A committed state in
  /// the same sense an attack or a dodge is, just one the combat state machine
  /// has no opinion about — landing is not a combat action.
  bool isLandLocked() const { return land_lock_timer > 0.0f; }

  /// Which dodge clip a world-space input direction calls for, resolved in the
  /// character's own frame. A directionless input backsteps.
  PlayerAnimState dodgeStateFor(Vector3 worldDirection) const;

  void resolveClips(const AssetManager &assets);

  /// The single prioritised ladder: the one place that answers "which clip".
  AnimSelection resolveAnimState(const UpdateContext &ctx, bool isMoving) const;

  /// Drives the animation clock from a selection, and returns the root-motion
  /// track of the clip that ended up playing.
  const RootMotion::Track &applyAnimSelection(const UpdateContext &ctx,
                                              const AnimSelection &selection);

  void updateLocomotionVelocity(const UpdateContext &ctx, Vector3 moveDirection);
  void applyRootMotion(const RootMotion::Track &track, float dt);

  Vector3 calculateCameraRelativeDirection(Vector3 camForward,
                                           Vector3 camRight) const;
  void handleCombatAndUtilityInputs(const UpdateContext &ctx);
};