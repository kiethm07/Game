#include <Entities/Player.h>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>

namespace {
/// Returned when there is no asset manager to ask for a track. Static rather
/// than a temporary so callers can hold a reference to it.
const RootMotion::Track kNoTrack{};
} // namespace

const Player::AnimDesc &Player::animDesc(PlayerAnimState state) {
  // Rows are in PlayerAnimState order. Function-local so the private
  // RUN_SPEED_SCALE is in scope.
  static const AnimDesc table[static_cast<int>(PlayerAnimState::Count)] = {
      //                clip             loop   rate             root   fadeIn
      /* Idle        */ {"Idle", true, 1.0f, false, 0.15f},
      /* Run         */ {"Run", true, RUN_SPEED_SCALE, false, 0.15f},
      /* Jump        */ {"Jump", false, 1.0f, false, 0.08f},
      // Loops, because a fall lasts as long as the drop does rather than as
      // long as the clip. The fade in is the longest in the table: it runs at
      // the apex, between two clips that already agree closely on the pose, so
      // there is nothing to be gained by making the swap crisp.
      /* Fall        */ {"Fall", true, 1.0f, false, 0.20f},
      // Entered 0.30s in, at LAND_CONTACT. Short fade the other way: the impact
      // is the whole point of the clip, and easing into it over a fifth of a
      // second reads as the character sinking to the floor rather than hitting
      // it.
      /* Land        */ {"Land", false, 1.0f, false, 0.06f, LAND_CONTACT},
      /* GuardImpact */ {"Impact", false, 1.0f, false, 0.05f},
      /* Guard       */ {"Block", false, 1.0f, false, 0.10f},
      // Root-driven, so each one's travel is exactly the sideways, forward or
      // backward distance it was authored with. The backstep is authored at
      // 1.67s against the others' 1.0s, half a second of which is the character
      // settling after the travel has finished; time-scaling it brings the
      // recovery — and so the length of the commitment, which is taken from the
      // scaled duration — into line with the other three.
      /* DodgeFwd    */ {"Dodge_Forward", false, 1.0f, true, 0.05f},
      /* DodgeBack   */ {"Dodge_Back", false, 1.5f, true, 0.05f},
      /* DodgeLeft   */ {"Dodge_Left", false, 1.0f, true, 0.05f},
      /* DodgeRight  */ {"Dodge_Right", false, 1.0f, true, 0.05f},
      // Fallback swing, used when an attack names no clip or names one the
      // loaded asset does not contain. Attacks that do name a clip override
      // this in resolveAnimState().
      /* Attack      */ {"Slash", false, 1.0f, false, 0.05f},
      /* HitReact    */ {"Impact_2", false, 1.0f, false, 0.05f},
      /* PostureBreak*/ {nullptr, false, 1.0f, false, 0.05f},
      /* Death       */ {nullptr, false, 1.0f, false, 0.10f},
  };
  return table[static_cast<int>(state)];
}

Player::Player(const InputManager &input_manager)
    : Character(Faction::Player), input_manager(input_manager) {
  combo = {AttackID::PlayerLight1, AttackID::PlayerLight2};
  position = {0, 0, 0};
  rotation = {0, 180.0f, 0};

  for (int i = 0; i < static_cast<int>(PlayerAnimState::Count); ++i)
    clip_index[i] = -1;
}

void Player::update(const UpdateContext &ctx) {
  const float dt = ctx.dt;

  if (ctx.assets)
    resolveClips(*ctx.assets);

  combat_component.update(dt);
  stats.update(dt);
  updateReaction(ctx);
  // Before the inputs, which are what take the character off the ground: a
  // jump pressed this frame must not be mistaken for a landing on it.
  updateLanding(ctx);

  handleCombatAndUtilityInputs(ctx);

  Vector3 moveDirection =
      calculateCameraRelativeDirection(ctx.camForward, ctx.camRight);

  // Two movement regimes. Free locomotion is code-driven so it stays
  // responsive and steerable; committed states (attacks, dodges, the first part
  // of a landing) hand control to the clip, so their travel is exactly what the
  // animator authored. Only the free regime produces velocity here — the
  // committed one gets its velocity below, from the root motion of whichever
  // clip is chosen, or none at all for a clip that does not travel.
  //
  // The landing is the one committed state the combat machine does not know
  // about, because landing is not a combat action; it is and-ed in here rather
  // than given a CombatState of its own.
  const bool freeMovement = combat_component.canMove() && !isLandLocked();
  if (freeMovement)
    updateLocomotionVelocity(ctx, moveDirection);

  const Vector3 velocity = getHorizontalVelocity();
  const bool isMoving = (velocity.x != 0.0f || velocity.z != 0.0f);

  const AnimSelection selection = resolveAnimState(ctx, isMoving);
  const RootMotion::Track &track = applyAnimSelection(ctx, selection);

  if (!freeMovement) {
    if (selection.rootDriven && track.hasMotion) {
      applyRootMotion(track, dt);
    } else {
      // No authored travel for this state: pin in place. Physics still applies
      // gravity and collisions this frame.
      setHorizontalVelocity({0.0f, 0.0f, 0.0f});
    }
  }

  prev_anim = selection;
}

void Player::resolveClips(const AssetManager &assets) {
  if (clips_resolved)
    return;
  clips_resolved = true;

  for (int i = 0; i < static_cast<int>(PlayerAnimState::Count); ++i) {
    const AnimDesc &desc = animDesc(static_cast<PlayerAnimState>(i));
    clip_index[i] =
        desc.clip ? assets.findAnimation(AssetID::PLAYER_WOLF, desc.clip) : -1;
  }

  const RootMotion::Track &runTrack = assets.getRootMotion(
      AssetID::PLAYER_WOLF, clip_index[static_cast<int>(PlayerAnimState::Run)]);
  if (runTrack.hasMotion) {
    // Only the controller's speed is set here; the matching clip time-scale is
    // the Run row's rate in the table, applied per frame.
    movement_component.setSpeed(runTrack.authoredSpeed * RUN_SPEED_SCALE);
    TraceLog(LOG_INFO,
             "Player: run clip authored at %.2f u/s, moving at %.2f u/s "
             "(playback %.2fx)",
             runTrack.authoredSpeed, runTrack.authoredSpeed * RUN_SPEED_SCALE,
             RUN_SPEED_SCALE);
  }
}

void Player::updateLocomotionVelocity(const UpdateContext &ctx,
                                      Vector3 moveDirection) {
  const float dt = ctx.dt;

  // Produce desired velocity + ease facing; PhysicsManager integrates position.
  Vector3 velocity = movement_component.resolve(moveDirection, rotation.y, dt);

  const bool airborne = !isGrounded();
  if (airborne) {
    // Carry take-off momentum. Writing the resolved velocity straight through
    // would stop the character dead in mid-air the moment the key is released,
    // because resolve() returns zero for zero input.
    const Vector3 momentum = getHorizontalVelocity();
    const bool steering = (moveDirection.x != 0.0f || moveDirection.z != 0.0f);

    if (!steering) {
      velocity = momentum;
    } else {
      // Move toward the desired velocity at a capped rate. A plain lerp here
      // would be frame-rate dependent and reach full ground speed within a few
      // frames, which is indistinguishable from full air control.
      Vector3 delta = Vector3Subtract(velocity, momentum);
      const float maxDelta = AIR_ACCELERATION * dt;
      if (Vector3Length(delta) > maxDelta) {
        delta = Vector3Scale(Vector3Normalize(delta), maxDelta);
      }
      velocity = Vector3Add(momentum, delta);
    }
    velocity.y = 0.0f;
  }

  setHorizontalVelocity(velocity);
}

Player::AnimSelection Player::resolveAnimState(const UpdateContext &ctx,
                                               bool isMoving) const {
  // Every rung is guarded on its clip existing, so a state whose animation the
  // loaded asset does not contain — including the three not authored yet —
  // falls through to the next one instead of freezing the character.
  const auto clipFor = [this](PlayerAnimState state) {
    return clip_index[static_cast<int>(state)];
  };
  const auto select = [&clipFor](PlayerAnimState state, unsigned variant,
                                 bool rootDriven) {
    return AnimSelection{state, variant, clipFor(state), rootDriven};
  };

  const CombatState combat = combat_component.getCurrentState();

  // Highest priority first. A committed state outranks the locomotion states
  // below it, which is what makes an attack or dodge started in mid-air show
  // its own clip rather than the jump.
  // Which of the four dodges was decided when the dodge started; this rung only
  // holds it for the length of the roll.
  if (combat == CombatState::Dodging && clipFor(dodge_state) >= 0)
    return select(dodge_state, combat_component.getActionId(), true);

  if (const AttackData *attack = combat_component.getActiveAttack()) {
    AnimSelection selection =
        select(PlayerAnimState::Attack, combat_component.getActionId(), false);

    // The attack's own clip when it names one the asset contains, otherwise the
    // table's generic swing. Without a fallback the animation would stay on
    // whatever locomotion clip was playing, so the attack would read as the
    // character standing still for its whole duration.
    if (attack->getClipName() && ctx.assets) {
      const int named = ctx.assets->findAnimation(AssetID::PLAYER_WOLF,
                                                  attack->getClipName());
      if (named >= 0) {
        selection.clip = named;
        selection.rootDriven = attack->usesRootMotion();
      }
    }

    if (selection.clip >= 0)
      return selection;
  }

  // Two clips for one condition, split at the apex. Rising is the jump proper;
  // once the arc turns over it is a fall, and a step off a ledge — which never
  // rises at all — goes straight to the fall without ever showing a launch it
  // did not perform. Falling is the looping half, since a drop lasts as long as
  // the geometry says rather than as long as the clip; the jump is not, so a
  // hang time longer than its clip settles on the last airborne pose instead of
  // launching a second time.
  if (!isGrounded()) {
    const bool rising = getVerticalVelocity() > 0.0f;
    if (!rising && clipFor(PlayerAnimState::Fall) >= 0)
      return select(PlayerAnimState::Fall, 0, false);
    if (clipFor(PlayerAnimState::Jump) >= 0)
      return select(PlayerAnimState::Jump, 0, false);
  }

  // Outranks the guard hold for the length of the reaction, and outranks
  // idle/run too, so releasing the button mid-flinch does not cut the hit
  // short. Attacks and dodges still cancel it — they are above this rung, so a
  // hit taken mid-swing does not interrupt the swing.
  if (reaction_timer > 0.0f && clipFor(reaction_state) >= 0)
    return select(reaction_state, reaction_id, false);

  // One rung for both halves of a guard. The parry window is the front of the
  // same raise the block hold sits on, so they share a clip and a state: the
  // raise runs during the parry, then Blocking picks it up mid-playback with no
  // visible re-trigger. Non-looping, so the clip ends guard-up and holds that
  // pose for as long as the button is down instead of replaying the raise.
  if (isGuarding() && clipFor(PlayerAnimState::Guard) >= 0)
    return select(PlayerAnimState::Guard, combat_component.getActionId(),
                  false);

  // Below everything that is an action, so a player who lands already swinging,
  // dodging or guarding does that instead of crouching — those cancel a landing
  // outright, and their own rungs above have already returned.
  //
  // Two ways to qualify. While the lock runs the landing is mandatory, and the
  // isMoving test is deliberately not consulted: the player is pinned this
  // frame, but the velocity read above is last frame's airborne momentum, which
  // has not been zeroed yet and would otherwise read as movement and skip the
  // clip on the very frame it is supposed to start. Once the lock lifts, the
  // clip keeps playing out its recovery only for as long as the player stays
  // put; the moment they run, the stride wins.
  if (land_timer > 0.0f && (isLandLocked() || !isMoving) &&
      clipFor(PlayerAnimState::Land) >= 0)
    return select(PlayerAnimState::Land, land_id, false);

  if (isMoving && clipFor(PlayerAnimState::Run) >= 0)
    return select(PlayerAnimState::Run, 0, false);

  return select(PlayerAnimState::Idle, 0, false);
}

const RootMotion::Track &
Player::applyAnimSelection(const UpdateContext &ctx,
                           const AnimSelection &selection) {
  const AnimDesc &desc = animDesc(selection.state);

  if (selection.clip >= 0) {
    // Read before play(), which is what changes it. A switch already starts its
    // own cross-fade, so the restart below must not fire on top and replace it
    // with a degenerate frame-0-to-frame-0 fade.
    const bool switched = (selection.clip != animation.index());

    animation.play(selection.clip, desc.loop, desc.fadeIn, desc.startAt);

    // Re-entering a state the clip is already playing: a combo step that reuses
    // the previous step's swing, or a second flinch during one guard. play() is
    // a no-op on the current clip, so those need an explicit rewind to read as
    // separate actions rather than one held pose — cross-fading out of the pose
    // being held, so the rewind eases rather than snaps.
    const bool reentered = (selection.state != prev_anim.state ||
                            selection.variant != prev_anim.variant);
    if (!switched && reentered)
      animation.restart(desc.fadeIn, desc.startAt);
  }

  // Only the run clip is time-scaled, per its table row. The idle clip is
  // in-place, so scaling it would just make the character fidget faster; the
  // jump's timing is tied to the physics arc rather than to ground speed; and
  // the block clip is a guard raise whose pace has nothing to do with how fast
  // the player is walking.
  //
  // Set after play(), so the fade snapshot above captures the outgoing state's
  // rate — a run fading out has to keep striding at 1.6x, not drop to the
  // incoming state's rate mid-fade.
  animation.setPlaybackRate(desc.rate);

  const RootMotion::Track &track =
      ctx.assets
          ? ctx.assets->getRootMotion(AssetID::PLAYER_WOLF, animation.index())
          : kNoTrack;
  animation.advance(ctx.dt, track.duration);
  return track;
}

void Player::updateReaction(const UpdateContext &ctx) {
  if (reaction_timer > 0.0f)
    reaction_timer -= ctx.dt;

  const PlayerAnimState queued = queued_reaction;
  queued_reaction = PlayerAnimState::Count;
  if (queued == PlayerAnimState::Count || !ctx.assets)
    return;

  const int clip = clip_index[static_cast<int>(queued)];
  if (clip < 0)
    return;

  // The clip's own length is the reaction's length — no separate constant to
  // fall out of sync when the animation is replaced.
  const RootMotion::Track &track =
      ctx.assets->getRootMotion(AssetID::PLAYER_WOLF, clip);
  if (track.duration <= 0.0f)
    return;

  // The newest hit wins outright rather than queueing behind the one playing:
  // a flinch that is already stale by the time it is shown reads as the
  // character reacting to the wrong hit.
  reaction_state = queued;
  reaction_timer = track.duration;
  reaction_id++;
}

void Player::updateLanding(const UpdateContext &ctx) {
  if (land_timer > 0.0f)
    land_timer -= ctx.dt;
  if (land_lock_timer > 0.0f)
    land_lock_timer -= ctx.dt;

  const bool grounded = isGrounded();

  if (!grounded) {
    // Sampled every airborne frame rather than read once on touchdown, because
    // PhysicsManager zeroes vertical velocity in the same pass that snaps the
    // character to the ground — by the time the landing is visible here, the
    // speed it happened at is gone.
    fall_speed = std::fmax(fall_speed, -getVerticalVelocity());
  } else if (!was_grounded) {
    // Touchdown, this frame only. A gentle one plays nothing: stepping off a
    // low ledge is not an event the character needs to acknowledge, and
    // interrupting the idle for it would make ordinary walking twitch.
    if (fall_speed >= LAND_TRIGGER_SPEED && ctx.assets) {
      const int clip = clip_index[static_cast<int>(PlayerAnimState::Land)];
      if (clip >= 0) {
        // The clip's own length is the recovery's length — no separate constant
        // to fall out of sync when the animation is replaced. Less the lead-in
        // the state skips, so the timer runs out with the clip rather than
        // holding the finished pose for the length of a descent already flown.
        const RootMotion::Track &track =
            ctx.assets->getRootMotion(AssetID::PLAYER_WOLF, clip);
        land_timer = track.duration - LAND_CONTACT;
        // Never longer than the clip it is holding the player through, so a
        // shorter landing animation cannot leave them frozen past its end.
        land_lock_timer = std::fmin(LAND_LOCK_DURATION, land_timer);
        land_id++;
      }
    }
    fall_speed = 0.0f;
  }

  was_grounded = grounded;
}

PlayerAnimState Player::dodgeStateFor(Vector3 worldDirection) const {
  // Nothing held: a standing dodge is a backstep. It is the reading that costs
  // the player nothing when they meant no direction at all, and the one that
  // opens the most distance from whatever they are backing away from.
  if (worldDirection.x == 0.0f && worldDirection.z == 0.0f)
    return PlayerAnimState::DodgeBack;

  // Into the character's own frame, which is the frame the clips were authored
  // in: +Z is forward and +X is the character's left. That is the same mapping
  // RootMotion::toWorld uses to send their travel back out, so the clip that
  // plays and the direction the player actually moves cannot disagree.
  const float yaw = rotation.y * DEG2RAD;
  const float sinYaw = std::sin(yaw);
  const float cosYaw = std::cos(yaw);
  const float forward = worldDirection.x * sinYaw + worldDirection.z * cosYaw;
  const float left = worldDirection.x * cosYaw - worldDirection.z * sinYaw;

  // The dominant axis wins outright. Four authored clips are not a blend space,
  // so a diagonal has to resolve to one of them; ties go to the forward/back
  // pair, which are the two the player is most likely to have meant when
  // holding a diagonal against a camera that is itself turning.
  if (std::fabs(forward) >= std::fabs(left))
    return forward > 0.0f ? PlayerAnimState::DodgeForward
                          : PlayerAnimState::DodgeBack;
  return left > 0.0f ? PlayerAnimState::DodgeLeft : PlayerAnimState::DodgeRight;
}

bool Player::isGuarding() const {
  const CombatState state = combat_component.getCurrentState();
  return state == CombatState::Parrying || state == CombatState::Blocking;
}

void Player::applyRootMotion(const RootMotion::Track &track, float dt) {
  if (dt <= 0.0f) {
    setHorizontalVelocity({0.0f, 0.0f, 0.0f});
    return;
  }

  // Expressed as a velocity rather than a position offset so it flows through
  // PhysicsManager's depenetration and ground snapping. Writing position
  // directly here would let a dodge pass through walls.
  Vector3 local =
      RootMotion::sampleDelta(track, animation.prevFrame(), animation.frame());
  Vector3 world = RootMotion::toWorld(local, rotation.y);
  Vector3 velocity = Vector3Scale(world, 1.0f / dt);

  // A dt spike (breakpoint, window drag) would otherwise turn one frame's
  // travel into an arbitrarily large velocity and tunnel through geometry.
  const float speed = Vector3Length(velocity);
  if (speed > MAX_ROOT_MOTION_SPEED) {
    velocity = Vector3Scale(velocity, MAX_ROOT_MOTION_SPEED / speed);
  }

  setHorizontalVelocity({velocity.x, 0.0f, velocity.z});
}

void Player::drawHPBar2D() const {
  float bar_width = 200.0f;
  float bar_height = 16.0f;

  int x = 20;
  int y = GetScreenHeight() - 40;

  float fill = stats.getHealthPercentage();

  DrawRectangle(x, y, (int)bar_width, (int)bar_height, DARKGRAY);
  DrawRectangle(x, y, (int)(bar_width * fill), (int)bar_height, LIME);
  DrawRectangleLines(x, y, (int)bar_width, (int)bar_height, WHITE);

  int posture_y = y + (int)bar_height + 4;
  float post_fill = stats.getPosturePercentage();
  DrawRectangle(x, posture_y, (int)bar_width, (int)bar_height, DARKGRAY);
  DrawRectangle(x, posture_y, (int)(bar_width * post_fill), (int)bar_height,
                ORANGE);
  DrawRectangleLines(x, posture_y, (int)bar_width, (int)bar_height, WHITE);
}

float Player::getColliderRadius() const { return BODY_RADIUS; }

float Player::getColliderHeight() const { return BODY_HEIGHT; }

std::vector<HurtBox> Player::getHurtBoxes() const {
  // Generate an upright 3D body capsule centered at position
  Capsule body_capsule =
      Capsule::createUpright(position, BODY_HEIGHT, BODY_RADIUS);
  return {HurtBox(body_capsule, getFaction(), getId())};
}

std::vector<HitBox> Player::getActiveHitBoxes() const {
  std::vector<HitBox> active_hitboxes;

  if (combat_component.getCurrentState() == CombatState::AttackActive) {
    float yaw_rad = rotation.y * DEG2RAD;
    Vector3 forward = {std::sin(yaw_rad), 0.0f, std::cos(yaw_rad)};

    // Position attack sphere in front of the player at chest height
    Vector3 hitbox_center = {position.x + forward.x * ATTACK_REACH,
                             position.y + (BODY_HEIGHT * 0.5f),
                             position.z + forward.z * ATTACK_REACH};

    Sphere attack_sphere(hitbox_center, ATTACK_RADIUS);

    active_hitboxes.emplace_back(attack_sphere,
                                 25.0f, // Health damage
                                 15.0f, // Posture damage
                                 getFaction(), getId());
  }

  return active_hitboxes;
}

void Player::takeDamage(float health_damage, float posture_damage) {
  // 1. Guard check state machine windows
  if (combat_component.getCurrentState() == CombatState::Parrying) {
    // Perfect deflect window: Ignore damage entirely!
    return;
  }

  const bool blocked =
      (combat_component.getCurrentState() == CombatState::Blocking);
  if (blocked) {
    // Blocking absorbs the HP damage entirely, but takes full posture damage
    health_damage = 0.0f;
  }

  // 2. Pass straight to Stats!
  bool hit_applied = stats.applyDamage(health_damage, posture_damage);

  if (hit_applied) {
    // Queued, not played here: this runs from CombatManager's pass, and the
    // reaction needs a frame's UpdateContext to find the clip's length. Gated
    // on the hit having connected, so an i-framed hit does not flinch.
    queued_reaction =
        blocked ? PlayerAnimState::GuardImpact : PlayerAnimState::HitReact;

    if (stats.isPostureBroken()) {
      // Stance broken state!
    } else if (stats.isDead()) {
      // Player death state!
    }
  }
}

Vector3 Player::calculateCameraRelativeDirection(Vector3 camForward,
                                                 Vector3 camRight) const {
  camForward.y = 0.0f;
  camRight.y = 0.0f;
  Vector3 direction = {0.0f, 0.0f, 0.0f};
  if (input_manager.isActionHeld(GameAction::MoveForward))
    direction = Vector3Add(direction, camForward);
  if (input_manager.isActionHeld(GameAction::MoveBackward))
    direction = Vector3Subtract(direction, camForward);
  if (input_manager.isActionHeld(GameAction::MoveRight))
    direction = Vector3Add(direction, camRight);
  if (input_manager.isActionHeld(GameAction::MoveLeft))
    direction = Vector3Subtract(direction, camRight);

  if (direction.x != 0.0f || direction.z != 0.0f) {
    direction = Vector3Normalize(direction);
  }
  return direction;
}

void Player::handleCombatAndUtilityInputs(const UpdateContext &ctx) {
  if (input_manager.isActionPressed(GameAction::Attack)) {
    combat_component.initiateCombo(combo);
  }
  if (input_manager.isActionPressed(GameAction::Parry)) {
    combat_component.startGuard();
  }
  if (input_manager.isActionReleased(GameAction::Parry)) {
    combat_component.stopGuard();
  }
  if (input_manager.isActionPressed(GameAction::Dodge) && ctx.assets) {
    // Read at the moment of the press, not per frame: the direction the player
    // was holding when they hit the button is the dodge they asked for, and
    // the clip has to be picked before it can say how long the dodge lasts.
    const PlayerAnimState dodge = dodgeStateFor(
        calculateCameraRelativeDirection(ctx.camForward, ctx.camRight));
    const int clip = clip_index[static_cast<int>(dodge)];

    if (clip >= 0) {
      // The clip's own length is the dodge's length — no separate constant to
      // fall out of sync when the animation is replaced. Divided by the
      // playback rate the table gives it, because a time-scaled clip finishes
      // proportionally sooner and the commitment has to end with it.
      const RootMotion::Track &track =
          ctx.assets->getRootMotion(AssetID::PLAYER_WOLF, clip);
      const float rate = animDesc(dodge).rate;

      // Latched only if the state machine accepted it. Otherwise a dodge
      // refused mid-attack would still leave its direction behind, and the
      // next accepted dodge would roll the wrong way.
      if (combat_component.startDodge(track.duration / rate))
        dodge_state = dodge;
    }
  }
  // Landing blocks the next jump as well as the run. Leaving it out would make
  // the lock trivially skippable by re-jumping on the frame of touchdown, which
  // is exactly the input a player hammering the button produces.
  if (input_manager.isActionPressed(GameAction::Jump) && isGrounded() &&
      combat_component.canMove() && !isLandLocked()) {
    // Vertical only. The bake deliberately leaves vertical motion off the root
    // bone (tools/bake_root_motion.py), so the arc comes from gravity here
    // rather than from the clip — which is also what lets the same jump clear
    // ledges of different heights.
    setVerticalVelocity(JUMP_SPEED);
    // Leave the ground on this frame so PhysicsManager starts integrating
    // gravity immediately; it only applies gravity to airborne characters.
    setGrounded(false);
  }
  if (input_manager.isActionPressed(GameAction::LockOn)) {
  }
}

CharacterRenderData Player::getRenderData() const {
  TransformData transform;
  transform.position = position;
  transform.rotation = rotation;
  transform.scale = {1.0f, 1.0f, 1.0f};

  AnimationState anim_state;
  anim_state.animIndex = animation.index();
  anim_state.animTime = animation.time();
  anim_state.blendFromIndex = animation.fadeFromIndex();
  anim_state.blendFromTime = animation.fadeFromTime();
  anim_state.blend = animation.blend();

  return {AssetID::PLAYER_WOLF, transform, anim_state};
}