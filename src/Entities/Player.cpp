#include <Entities/Player.h>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>

Player::Player(const InputManager &input_manager)
    : Character(Faction::Player), input_manager(input_manager) {
  combo = {AttackID::PlayerLight1, AttackID::PlayerLight2};
  position = {0, 0, 0};
  rotation = {0, 180.0f, 0};
}

void Player::update(const UpdateContext &ctx) {
  const float dt = ctx.dt;

  if (ctx.assets)
    resolveClips(*ctx.assets);

  combat_component.update(dt);
  stats.update(dt);

  handleCombatAndUtilityInputs(ctx);

  Vector3 moveDirection =
      calculateCameraRelativeDirection(ctx.camForward, ctx.camRight);

  // Two movement regimes. Free locomotion is code-driven so it stays
  // responsive and steerable; committed states (attacks, dodges) hand control
  // to the clip, so their travel is exactly what the animator authored.
  if (combat_component.canMove()) {
    updateLocomotion(ctx, moveDirection);
  } else {
    updateCommittedState(ctx);
  }

  prev_combat_state = combat_component.getCurrentState();
}

void Player::resolveClips(const AssetManager &assets) {
  if (clips.resolved)
    return;
  clips.resolved = true;

  clips.idle = assets.findAnimation(AssetID::PLAYER_WOLF, "Idle");
  clips.run = assets.findAnimation(AssetID::PLAYER_WOLF, "Run");
  // Stand-in: the sword-and-shield pack has no dodge or roll. Strafe travels
  // 1.30 units over 1.17s — near-identical to the Sneak clip this replaced —
  // so it still exercises the root-motion path end to end. Swap the name once a
  // real dodge clip is baked.
  clips.dodge = assets.findAnimation(AssetID::PLAYER_WOLF, "Strafe");
  // Jump_2, not Jump: Jump travels 2.45 units, which would fight the physics
  // arc that JUMP_SPEED and gravity already drive. Jump_2 is in place, leaving
  // the whole trajectory to the controller.
  clips.jump = assets.findAnimation(AssetID::PLAYER_WOLF, "Jump_2");

  // Fallback only. Attacks normally name their own clip in AttackRegistry, so
  // each combo step can differ; this is what plays when one doesn't, or when
  // the clip it names is missing from the loaded asset.
  clips.attack = assets.findAnimation(AssetID::PLAYER_WOLF, "Slash");

  // Locomotion runs at the run clip's authored speed, scaled for game feel.
  // Playing the clip back at the same ratio keeps the stride matched to the
  // ground travel, so the tuning knob can never reintroduce foot sliding.
  const RootMotion::Track &runTrack =
      assets.getRootMotion(AssetID::PLAYER_WOLF, clips.run);
  if (runTrack.hasMotion) {
    movement_component.setSpeed(runTrack.authoredSpeed * RUN_SPEED_SCALE);
    animation.setPlaybackRate(RUN_SPEED_SCALE);
    TraceLog(LOG_INFO,
             "Player: run clip authored at %.2f u/s, moving at %.2f u/s "
             "(playback %.2fx)",
             runTrack.authoredSpeed, runTrack.authoredSpeed * RUN_SPEED_SCALE,
             RUN_SPEED_SCALE);
  }
}

void Player::updateLocomotion(const UpdateContext &ctx, Vector3 moveDirection) {
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

  const bool isMoving = (velocity.x != 0.0f || velocity.z != 0.0f);
  if (airborne) {
    // Non-looping: the clip holds on its last frame until touchdown, so a jump
    // that outlasts the animation settles into the landing pose rather than
    // restarting the launch.
    animation.play(clips.jump, false);
  } else {
    animation.play(isMoving ? clips.run : clips.idle, true);
  }

  // Only the run clip is time-scaled. The idle clip is in-place, so scaling it
  // would just make the character fidget faster, and the jump's timing is tied
  // to the physics arc rather than to ground speed.
  animation.setPlaybackRate((!airborne && isMoving) ? RUN_SPEED_SCALE : 1.0f);

  const RootMotion::Track &track =
      ctx.assets
          ? ctx.assets->getRootMotion(AssetID::PLAYER_WOLF, animation.index())
          : RootMotion::Track{};
  animation.advance(dt, track.duration);
}

void Player::updateCommittedState(const UpdateContext &ctx) {
  const float dt = ctx.dt;

  // Committed states never steer, so playback is always at natural rate.
  animation.setPlaybackRate(1.0f);

  int clipIndex = -1;
  bool rootDriven = false;
  bool restartClip = false;

  if (combat_component.getCurrentState() == CombatState::Dodging) {
    clipIndex = clips.dodge;
    rootDriven = true;
  } else if (const AttackData *attack = combat_component.getActiveAttack()) {
    if (attack->getClipName() && ctx.assets) {
      clipIndex = ctx.assets->findAnimation(AssetID::PLAYER_WOLF,
                                            attack->getClipName());
      rootDriven = attack->usesRootMotion();
    }

    // Fall back to the generic swing when the attack names no clip, or names
    // one the loaded asset does not contain. Without this the animation stays
    // on whatever locomotion clip was playing, so the attack reads as the
    // character standing still for its whole duration.
    if (clipIndex < 0) {
      clipIndex = clips.attack;
      rootDriven = false;
    }

    // Every combo step starts here. If it reuses the previous step's clip —
    // which both fallbacks above always do — play() would see the index it is
    // already on and leave the swing held at its end frame.
    restartClip = (combat_component.getCurrentState() ==
                   CombatState::AttackStartup) &&
                  (prev_combat_state != CombatState::AttackStartup);
  }

  if (clipIndex >= 0) {
    animation.play(clipIndex, false);
    if (restartClip)
      animation.restart();
  }

  const RootMotion::Track &track =
      ctx.assets
          ? ctx.assets->getRootMotion(AssetID::PLAYER_WOLF, animation.index())
          : RootMotion::Track{};
  animation.advance(dt, track.duration);

  if (rootDriven && track.hasMotion) {
    applyRootMotion(track, dt);
  } else {
    // No authored travel for this state: pin in place. Physics still applies
    // gravity and collisions this frame.
    setHorizontalVelocity({0.0f, 0.0f, 0.0f});
  }
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

  if (combat_component.getCurrentState() == CombatState::Blocking) {
    // Blocking cuts HP damage in half, but takes full posture damage
    health_damage = 0.0f;
  }

  // 2. Pass straight to Stats!
  bool hit_applied = stats.applyDamage(health_damage, posture_damage);

  if (hit_applied) {
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
  if (input_manager.isActionPressed(GameAction::Dodge) && ctx.assets &&
      clips.dodge >= 0) {
    // The clip's own length is the dodge's length — no separate constant to
    // fall out of sync when the animation is replaced.
    const RootMotion::Track &track =
        ctx.assets->getRootMotion(AssetID::PLAYER_WOLF, clips.dodge);
    combat_component.startDodge(track.duration);
  }
  if (input_manager.isActionPressed(GameAction::Jump) && isGrounded() &&
      combat_component.canMove()) {
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

  return {AssetID::PLAYER_WOLF, transform, anim_state};
}