#include <Entities/Player.h>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>

Player::Player(const InputManager &input_manager)
    : Character(Faction::Player), input_manager(input_manager) {
  stats = Stats(1000.0f, 100.0f, 15.0f);
  combo = {AttackID::PlayerLight1, AttackID::PlayerLight2};
  execution_combo = {AttackID::PlayerExecution};
  position = {0, 0, 0};
  rotation = {0, 180.0f, 0};
}

void Player::update(const UpdateContext &ctx) {
  const float dt = ctx.dt;

  if (ctx.assets && animator.resolveClips(*ctx.assets))
    movement_component.setSpeed(animator.locomotionSpeed());

  combat_component.update(dt);
  stats.update(dt);

  // Before the inputs, which are what take the character off the ground: a jump
  // pressed this frame must not be mistaken for a landing on it, and must not
  // slip past a gate evaluated before the stagger it would be escaping.
  //
  // A stagger cancels whatever was running. An attack whose root motion carried
  // the player off a ledge would otherwise keep a live hitbox through a landing
  // they no longer control.
  if (locomotion.update(dt, isGrounded(), getVerticalVelocity(),
                        animator.landPlayDuration(ctx.assets)))
    combat_component.interrupt();

  animator.updateFlinch(dt, ctx.assets);

  // Held, never pressed: the press frame is the dodge, which
  // handleCombatAndUtilityInputs takes below, and InputManager reports a key as
  // Pressed *or* Held but never both. So the earliest this can be true is the
  // frame after the dash began — which is exactly the reading that makes a hold
  // outlasting the dodge, and nothing shorter, a sprint.
  const bool sprint_held = input_manager.isActionHeld(GameAction::Dodge);

  handleCombatAndUtilityInputs(
      ctx, locomotion.gate(combat_component, isGrounded(), sprint_held));

  // Re-evaluated after the inputs rather than reusing the one above. An attack
  // started this frame has to stop movement on this frame; a gate read before
  // the input that began it would let one frame of free steering through.
  const ActionGate move_gate =
      locomotion.gate(combat_component, isGrounded(), sprint_held);
  gait = move_gate.gait;

  // Two movement regimes. Free locomotion is code-driven so it stays responsive
  // and steerable; committed states (attacks, dodges, a landing stagger) hand
  // control to the clip, so their travel is exactly what the animator authored.
  // Only the free regime produces velocity here — the committed one gets its
  // velocity below, from the root motion of whichever clip is chosen, or none
  // at all for a clip that does not travel.
  if (move_gate.canMove)
    updateLocomotionVelocity(
        ctx, calculateCameraRelativeDirection(ctx.camForward, ctx.camRight),
        move_gate.moveSpeedScale);

  const Vector3 velocity = getHorizontalVelocity();

  PlayerAnimator::Frame frame;
  frame.combat = &combat_component;
  frame.assets = ctx.assets;
  frame.grounded = isGrounded();
  frame.verticalVelocity = getVerticalVelocity();
  frame.moving = (velocity.x != 0.0f || velocity.z != 0.0f);
  frame.staggered = locomotion.isStaggered();
  frame.landingVisible = locomotion.isLandingVisible();
  frame.landId = locomotion.landId();
  frame.stance = locomotion.getStance();
  frame.gait = move_gate.gait;
  frame.speedScale = move_gate.moveSpeedScale;

  const PlayerAnimator::Result anim = animator.update(frame, dt);

  if (!move_gate.canMove) {
    if (anim.rootDriven && anim.track->hasMotion) {
      applyRootMotion(*anim.track, dt);
    } else {
      // No authored travel for this state: pin in place. Physics still applies
      // gravity and collisions this frame.
      setHorizontalVelocity({0.0f, 0.0f, 0.0f});
    }
  }
}

bool Player::isDashing() const {
  return gait == Gait::Sprinting ||
         combat_component.getCurrentState() == CombatState::Dodging;
}

bool Player::isExecuting() const {
  // Identity, not equality: the registry hands out references to entries in an
  // unordered_map that is filled once and never touched again, so the address
  // of an attack's data is a stable name for that attack. getActiveAttack()
  // returns null outside the three attack phases, which compares false here.
  return combat_component.getActiveAttack() ==
         &AttackRegistry::instance().getAttackData(AttackID::PlayerExecution);
}

void Player::updateLocomotionVelocity(const UpdateContext &ctx,
                                      Vector3 moveDirection,
                                      float speedScale) {
  const float dt = ctx.dt;

  // Produce desired velocity + ease facing; PhysicsManager integrates position.
  // The gate's scale is applied to the resolved velocity rather than to the
  // component's speed, which is bound to the run clip's authored speed and set
  // once at clip resolution — writing it per frame would fight that.
  Vector3 velocity = movement_component.resolve(moveDirection, rotation.y, dt);
  velocity = Vector3Scale(velocity, speedScale);

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

void Player::applyRootMotion(const RootMotion::Track &track, float dt) {
  if (dt <= 0.0f) {
    setHorizontalVelocity({0.0f, 0.0f, 0.0f});
    return;
  }

  // Expressed as a velocity rather than a position offset so it flows through
  // PhysicsManager's depenetration and ground snapping. Writing position
  // directly here would let a dodge pass through walls.
  Vector3 local = animator.sampleRootDelta(track);
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

  if (locomotion.getStance() == Stance::Crouching) {
      DrawText("CROUCHING", x, y - 24, 20, SKYBLUE);
  }
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

void Player::takeDamage(float health_damage, float posture_damage, Character* attacker) {
  // 1. Guard check state machine windows
  if (combat_component.getCurrentState() == CombatState::Parrying) {
    // Perfect deflect window: Ignore health damage entirely.
    // Receive drastically more posture damage than blocking, but it never breaks posture.
    float parry_posture_damage = posture_damage * 2.0f;
    float current = stats.getCurrentPosture();
    float max_p = stats.getMaxPosture();

    if (current + parry_posture_damage >= max_p) {
      float safe_damage = (max_p - current) - 0.1f;
      if (safe_damage > 0.0f) {
        stats.applyDamage(0.0f, safe_damage);
      }
    } else {
      stats.applyDamage(0.0f, parry_posture_damage);
    }

    // Attacker takes posture damage from being parried
    if (attacker) {
      attacker->takeDamage(0.0f, posture_damage * 1.5f, nullptr);
    }
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
    // reaction needs a frame's assets to find the clip's length. Gated on the
    // hit having connected, so an i-framed hit does not flinch.
    animator.queueReaction(blocked);

    if (stats.isPostureBroken()) {
      // 2.0s duration for posture break stagger
      combat_component.breakPosture(2.0f);
      stats.resetPosture();
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

void Player::handleCombatAndUtilityInputs(const UpdateContext &ctx,
                                          const ActionGate &gate) {
  if (input_manager.isActionPressed(GameAction::Attack) && gate.canAttack) {
    combat_component.initiateCombo(combo);
  }
  if (input_manager.isActionPressed(GameAction::Parry) && gate.canGuard) {
    combat_component.startGuard();
  }
  if (input_manager.isActionReleased(GameAction::Parry)) {
    // Not gated: releasing a guard is letting go of a commitment, not starting
    // one. A stagger that swallowed the release would leave the guard stuck up
    // once it recovered.
    combat_component.stopGuard();
  }
  if (input_manager.isActionPressed(GameAction::Dodge) && gate.canDodge &&
      ctx.assets) {
    // Read at the moment of the press, not per frame: the direction the player
    // was holding when they hit the button is the dodge they asked for, and
    // the clip has to be picked before it can say how long the dodge lasts.
    const PlayerAnimState dodge = animator.dodgeStateFor(
        calculateCameraRelativeDirection(ctx.camForward, ctx.camRight),
        rotation.y);
    const float duration = animator.dodgeDuration(*ctx.assets, dodge);

    // Latched only if the state machine accepted it. Otherwise a dodge refused
    // mid-attack would still leave its direction behind, and the next accepted
    // dodge would roll the wrong way.
    if (combat_component.startDodge(duration))
      animator.setDodge(dodge);
  }
  if (input_manager.isActionPressed(GameAction::Jump) && gate.canJump) {
    // A jump taken from a crouch stands up first rather than launching
    // crouched.
    locomotion.standUp();

    // Vertical only. The bake deliberately leaves vertical motion off the root
    // bone (tools/bake_root_motion.py), so the arc comes from gravity here
    // rather than from the clip — which is also what lets the same jump clear
    // ledges of different heights.
    setVerticalVelocity(JUMP_SPEED);
    // Leave the ground on this frame so PhysicsManager starts integrating
    // gravity immediately; it only applies gravity to airborne characters.
    setGrounded(false);
  }
  if (input_manager.isActionPressed(GameAction::Crouch)) {
      if (locomotion.getStance() == Stance::Crouching) {
          locomotion.setStance(Stance::Standing);
      } else {
          locomotion.setStance(Stance::Crouching);
      }
  }
  if (input_manager.isActionPressed(GameAction::LockOn)) {
  }
}

CharacterRenderData Player::getRenderData() const {
  TransformData transform;
  transform.position = position;
  transform.rotation = rotation;
  transform.scale = {1.0f, 1.0f, 1.0f};

  return {AssetID::PLAYER_WOLF, transform, animator.renderState()};
}
