#include <Entities/Player.h>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>
#include <GameManager/SmokeCloud.h>
#include <GameManager/SoundController.h>
#include <CombatData/AttackRegistry.h>
#include <Entities/Items/HealingGourd.h>
#include <Entities/Items/SmokeBomb.h>
#include <Rendering/BoneSocketHelper.h>
#include <Rendering/PostureMeter.h>

Player::Player(const InputManager &input_manager)
    : Character(Faction::Player), input_manager(input_manager) {
  stats = Stats(1000.0f, 100.0f, 15.0f);
  combo = {AttackID::PlayerLight1, AttackID::PlayerLight2};
  execution_combo = {AttackID::PlayerExecution};
  position = {0, 0, 0};
  rotation = {0, 180.0f, 0};

  sword_trail.setColors({255, 255, 255, 240}, {100, 210, 255, 200});

  inventory.push_back(std::make_unique<HealingGourd>());
  inventory.push_back(std::make_unique<SmokeBomb>());
}

void Player::update(const UpdateContext &ctx) {
  const float dt = ctx.dt;

  if (ctx.assets && animator.resolveClips(*ctx.assets))
    movement_component.setSpeed(animator.locomotionSpeed());

  combat_component.update(dt);
  stats.update(dt);
  sword_trail.update(dt);

  // Out of health: the fall, and nothing else.
  //
  // Ahead of the gate, the inputs and the locomotion rather than expressed as
  // another ActionGate rule, because death is not a restriction on what the
  // player may start — it is the end of them starting anything at all, and a
  // gate with every field false would still leave the ladder below choosing
  // between an idle and a stride. Returning here is what makes the clip the
  // only thing on screen.
  //
  // Physics still runs on this character afterwards, from GameplayState's own
  // pass: gravity and ground snapping are what put the body on the floor it
  // died over rather than leaving it hanging where the last hit landed.
  if (stats.isDead()) {
    // Once, on the frame of death. interrupt() is what retires a swing that was
    // mid-flight — it owns a live hitbox, and a corpse must not still be able
    // to kill what killed it.
    if (!death_handled) {
      death_handled = true;
      combat_component.interrupt();
      cancelItemUse();
      sword_trail.clear();
      if (ctx.sound_controller != nullptr) {
        ctx.sound_controller->stopSFX(AssetID::SFX_WALK);
        ctx.sound_controller->stopSFX(AssetID::SFX_RUN);
      }
    }

    setHorizontalVelocity({0.0f, 0.0f, 0.0f});

    PlayerAnimator::Frame frame;
    frame.combat = &combat_component;
    frame.assets = ctx.assets;
    frame.grounded = isGrounded();
    frame.verticalVelocity = getVerticalVelocity();
    frame.dead = true;
    animator.update(frame, dt);
    return;
  }

  // Before the inputs, which are what take the character off the ground: a jump
  // pressed this frame must not be mistaken for a landing on it, and must not
  // slip past a gate evaluated before the stagger it would be escaping.
  //
  // A stagger cancels whatever was running. An attack whose root motion carried
  // the player off a ledge would otherwise keep a live hitbox through a landing
  // they no longer control.
  if (locomotion.update(dt, isGrounded(), getVerticalVelocity(),
                        animator.landPlayDuration(ctx.assets))) {
    combat_component.interrupt();
    sword_trail.clear();
    if (ctx.sound_controller != nullptr) {
      ctx.sound_controller->stopSFX(AssetID::SFX_WALK);
      ctx.sound_controller->stopSFX(AssetID::SFX_RUN);
    }
  }

  animator.updateFlinch(dt, ctx.assets);

  // Held, never pressed: the press frame is the dodge, which
  // handleCombatAndUtilityInputs takes below, and InputManager reports a key as
  // Pressed *or* Held but never both. So the earliest this can be true is the
  // frame after the dash began — which is exactly the reading that makes a hold
  const bool sprint_held = input_manager.isActionHeld(GameAction::Dodge);

  in_smoke_flag = false;
  if (ctx.smoke_clouds) {
      for (const auto& smoke : *ctx.smoke_clouds) {
          if (Vector3DistanceSqr(position, smoke.position) <= smoke.radius * smoke.radius) {
              in_smoke_flag = true;
              break;
          }
      }
  }

  ActionGate input_gate =
      locomotion.gate(combat_component, isGrounded(), sprint_held);
  if (animator.isFlinching()) {
    input_gate.canMove = false;
    input_gate.canAttack = false;
    input_gate.canDodge = false;
    input_gate.canJump = false;
  }

  handleCombatAndUtilityInputs(ctx, input_gate);

  // Re-evaluated after the inputs rather than reusing the one above. An attack
  // started this frame has to stop movement on this frame; a gate read before
  // the input that began it would let one frame of free steering through.
  ActionGate move_gate =
      locomotion.gate(combat_component, isGrounded(), sprint_held);
  if (animator.isFlinching()) {
    move_gate.canMove = false;
    move_gate.canAttack = false;
    move_gate.canDodge = false;
    move_gate.canJump = false;
  }

  // Item timer logic
  if (item_use_timer > 0.0f) {
      item_use_timer -= dt;
      move_gate.moveSpeedScale = 0.5f; // Slow down while using item
      if (item_use_timer <= 0.0f) {
          item_use_timer = 0.0f;
          if (!inventory.empty() && active_item_index < inventory.size()) {
              inventory[active_item_index]->use(this);
              inventory[active_item_index]->consume();
              if (ctx.sound_controller != nullptr) {
                  ctx.sound_controller->stopSFX(AssetID::SFX_WALK);
                  ctx.sound_controller->stopSFX(AssetID::SFX_RUN);
                  if (active_item_index == 0) {
                      ctx.sound_controller->playSFX(AssetID::SFX_HEAL);
                  } else {
                      ctx.sound_controller->playSFX(AssetID::SFX_SMOKE);
                  }
              }
          }
      }
  }

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
        move_gate.moveSpeedScale, move_gate.gait);

  const Vector3 velocity = getHorizontalVelocity();

  // Landing audio trigger
  if (isGrounded() && !was_grounded_audio) {
      if (ctx.sound_controller != nullptr) {
          ctx.sound_controller->stopSFX(AssetID::SFX_WALK);
          ctx.sound_controller->stopSFX(AssetID::SFX_RUN);
          ctx.sound_controller->playSFX(AssetID::SFX_LAND);
      }
  }
  was_grounded_audio = isGrounded();

  // Footstep audio trigger
  const bool is_moving = (velocity.x != 0.0f || velocity.z != 0.0f);
  if (isGrounded() && move_gate.canMove && is_moving) {
      if (ctx.sound_controller != nullptr) {
          if (move_gate.gait == Gait::Sprinting) {
              ctx.sound_controller->stopSFX(AssetID::SFX_WALK);
              if (!ctx.sound_controller->isSFXPlaying(AssetID::SFX_RUN)) {
                  ctx.sound_controller->playSFX(AssetID::SFX_RUN);
              }
          } else {
              ctx.sound_controller->stopSFX(AssetID::SFX_RUN);
              if (!ctx.sound_controller->isSFXPlaying(AssetID::SFX_WALK)) {
                  ctx.sound_controller->playSFX(AssetID::SFX_WALK);
              }
          }
      }
  } else {
      if (ctx.sound_controller != nullptr) {
          ctx.sound_controller->stopSFX(AssetID::SFX_WALK);
          ctx.sound_controller->stopSFX(AssetID::SFX_RUN);
      }
  }

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
  frame.lockedOn = (ctx.lockedTarget != nullptr && move_gate.gait != Gait::Sprinting);
  
  if (frame.lockedOn && move_gate.canMove) {
    // Determine local move direction for strafing
    Vector3 worldDir = calculateCameraRelativeDirection(ctx.camForward, ctx.camRight);
    float yawRad = rotation.y * DEG2RAD;
    float sinYaw = std::sin(yawRad);
    float cosYaw = std::cos(yawRad);
    // +Z is forward, +X is left in this engine's animation local space
    frame.localMoveDir.z = worldDir.x * sinYaw + worldDir.z * cosYaw;
    frame.localMoveDir.x = worldDir.x * cosYaw - worldDir.z * sinYaw;
  } else {
    frame.localMoveDir = {0.0f, 0.0f, 0.0f};
  }

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

  if (combat_component.getCurrentState() == CombatState::AttackActive && ctx.assets != nullptr) {
    const AttackData *attack = combat_component.getActiveAttack();
    if (attack != nullptr && attack->hasTrail()) {
      Vector3 world_base = {0.0f, 0.0f, 0.0f};
      Vector3 world_tip = {0.0f, 0.0f, 0.0f};
      CharacterRenderData render_data = getRenderData();

      bool sampled = BoneSocketHelper::sampleSwordPoints(
          *const_cast<AssetManager *>(ctx.assets),
          render_data,
          world_base,
          world_tip,
          attack->getBladeVector(),
          attack->getHiltVector());

      if (sampled) {
        sword_trail.addSegment(world_base, world_tip, attack->getTrailDuration());
      }
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
                                      Vector3 moveDirection, float speedScale, Gait gait) {
  const float dt = ctx.dt;

  // Produce desired velocity + ease facing; PhysicsManager integrates position.
  // The gate's scale is applied to the resolved velocity rather than to the
  // component's speed, which is bound to the run clip's authored speed and set
  // once at clip resolution — writing it per frame would fight that.
  
  Vector3 velocity = {0.0f, 0.0f, 0.0f};
  if (ctx.lockedTarget && gait != Gait::Sprinting) {
    // If locked on, always face the target
    Vector3 to_target = Vector3Subtract(ctx.lockedTarget->getPosition(), position);
    to_target.y = 0.0f;
    if (Vector3LengthSqr(to_target) > 0.001f) {
      to_target = Vector3Normalize(to_target);
      float target_yaw = std::atan2(to_target.x, to_target.z) * RAD2DEG;
      
      // Shortest angular distance
      float angle_diff = target_yaw - rotation.y;
      while (angle_diff < -180.0f) angle_diff += 360.0f;
      while (angle_diff > 180.0f) angle_diff -= 360.0f;
      
      float alpha = 10.0f * dt; // Same as ROTATION_SPEED in MovementComponent
      if (alpha > 1.0f) alpha = 1.0f;
      rotation.y += angle_diff * alpha;
      
      while (rotation.y < 0.0f) rotation.y += 360.0f;
      while (rotation.y >= 360.0f) rotation.y -= 360.0f;
    }
    // Set velocity without altering facing again
    velocity = {moveDirection.x * movement_component.getSpeed(), 0.0f, moveDirection.z * movement_component.getSpeed()};
  } else {
    velocity = movement_component.resolve(moveDirection, rotation.y, dt);
  }
  
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
  if (combat_component.getCurrentState() == CombatState::Dodging) {
      local = Vector3Scale(local, 1.5f);
  }
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

void Player::drawHPBar2D(bool engaged) const {
  float bar_width = 200.0f;
  float bar_height = 16.0f;

  int x = 20;
  int y = GetScreenHeight() - 40;

  float fill = stats.getHealthPercentage();

  DrawRectangle(x, y, (int)bar_width, (int)bar_height, DARKGRAY);
  DrawRectangle(x, y, (int)(bar_width * fill), (int)bar_height, LIME);
  DrawRectangleLines(x, y, (int)bar_width, (int)bar_height, WHITE);

  // Posture is not stacked under health any more: it sits centred on the bottom
  // of the screen, under the player, where it is read without looking away from
  // the fight. `engaged` is the caller's answer to "is there a fight" -- it can
  // see the lock and every enemy's awareness and this cannot -- but posture
  // above zero is reason enough on its own, so that a bar the player is
  // watching drain cannot vanish the moment the last enemy loses sight of them.
  if (engaged || stats.getCurrentPosture() > 0.0f) {
    PostureMeter::Style style;
    style.half_width = GetScreenWidth() * 0.14f;
    style.height = 11.0f;
    style.cap = style.half_width * 0.13f;
    style.fill = PostureMeter::kPlayerFill;

    PostureMeter::draw(GetScreenWidth() * 0.5f, GetScreenHeight() - 60.0f,
                       stats.getPosturePercentage(), style);
  }

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
    const AttackData* active_attack = combat_component.getActiveAttack();
    if (!active_attack) return active_hitboxes;

    float yaw_rad = rotation.y * DEG2RAD;
    Vector3 forward = {std::sin(yaw_rad), 0.0f, std::cos(yaw_rad)};
    Vector3 right = {-std::cos(yaw_rad), 0.0f, std::sin(yaw_rad)};
    Vector3 up = {0.0f, 1.0f, 0.0f};

    for (const auto& def : active_attack->getHitBoxDefs(combat_component.getActiveSwing())) {
        if (def.type == HitBoxShapeType::Sphere) {
            Vector3 center = position;
            center = Vector3Add(center, Vector3Scale(forward, def.forward_offset));
            center = Vector3Add(center, Vector3Scale(up, def.vertical_offset));
            
            Sphere sphere(center, def.radius);
            active_hitboxes.emplace_back(sphere, def.health_damage, def.posture_damage, getFaction(), getId());
        } else if (def.type == HitBoxShapeType::Capsule) {
            Vector3 start = position;
            start = Vector3Add(start, Vector3Scale(right, def.start_offset.x));
            start = Vector3Add(start, Vector3Scale(up, def.start_offset.y));
            start = Vector3Add(start, Vector3Scale(forward, def.start_offset.z));

            Vector3 end = position;
            end = Vector3Add(end, Vector3Scale(right, def.end_offset.x));
            end = Vector3Add(end, Vector3Scale(up, def.end_offset.y));
            end = Vector3Add(end, Vector3Scale(forward, def.end_offset.z));

            Capsule capsule(start, end, def.capsule_radius);
            active_hitboxes.emplace_back(capsule, def.health_damage, def.posture_damage, getFaction(), getId());
        }
    }
  }

  return active_hitboxes;
}

DamageResult Player::takeDamage(float health_damage, float posture_damage,
                                Character *attacker) {
  if (isExecuting()) {
    return DamageResult::IGNORED;
  }

  bool can_block = true;
  if (attacker) {
    float yaw_rad = rotation.y * DEG2RAD;
    Vector3 forward = {std::sin(yaw_rad), 0.0f, std::cos(yaw_rad)};
    Vector3 to_attacker = {attacker->getPosition().x - position.x, 0.0f, attacker->getPosition().z - position.z};
    to_attacker = Vector3Normalize(to_attacker);
    if (Vector3DotProduct(forward, to_attacker) < 0.707f) {
      can_block = false;
    }
  }

  // 1. Guard check state machine windows
  if (can_block && combat_component.getCurrentState() == CombatState::Parrying) {
    // Perfect deflect window: Ignore health damage entirely.
    // Receive drastically more posture damage than blocking, but it never
    // breaks posture.
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

    // Attacker takes posture damage from being parried. Through the deflect
    // hook, not takeDamage: the clash costs them posture without making them
    // react as though the blow had landed on them.
    if (attacker) {
      attacker->onAttackDeflected(posture_damage * 1.5f);
    }

    // The deflect caught something, so the anti-spam penalty startGuard() armed
    // is withdrawn: the next guard gets its full window however soon it comes.
    // This is what lets a combo thrown faster than PARRY_SPAM_COOLDOWN be
    // deflected hit for hit instead of collapsing to the 0.05s punish window
    // from its second swing onward.
    combat_component.notifyParrySuccess();
    return DamageResult::PARRIED;
  }

  const bool blocked =
      (can_block && combat_component.getCurrentState() == CombatState::Blocking);
  if (blocked) {
    // Blocking absorbs the HP damage entirely, but takes full posture damage
    health_damage = 0.0f;
  }

  // 2. Pass straight to Stats!
  bool hit_applied = stats.applyDamage(health_damage, posture_damage);

  if (hit_applied) {
    // Cancel item usage on flinch
    cancelItemUse();
    sword_trail.clear();
    setHorizontalVelocity({0.0f, 0.0f, 0.0f});

    // Queued, not played here: this runs from CombatManager's pass, and the
    // reaction needs a frame's assets to find the clip's length. Gated on the
    // hit having connected, so an i-framed hit does not flinch.
    animator.queueReaction(blocked);

    if (stats.isPostureBroken()) {
      // 2.0s duration for posture break stagger
      combat_component.breakPosture(2.0f);
      stats.resetPosture();
    }
    // Nothing here for the killing blow. Death is not an event the damage pass
    // reacts to — update() reads stats.isDead() at the top of the next frame
    // and takes the character over from there. Acting on it here would put the
    // fall a frame ahead of the flinch queued three lines up, and would have
    // this pass, which runs from CombatManager, reaching into the animation
    // clock that only update() advances.
    if (blocked) {
      return DamageResult::BLOCKED;
    }
    return DamageResult::HIT;
  }
  return DamageResult::IGNORED;
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
  // Ahead of the item-use return below, so a guard press cannot sit latched
  // through a heal and fire the moment it ends. Decayed before the press is
  // latched, so a press arriving this frame still gets its whole window.
  if (guard_buffer_timer > 0.0f)
    guard_buffer_timer -= ctx.dt;

  // If we are currently using an item, block combat actions
  if (item_use_timer > 0.0f) {
      return;
  }

  // Item cycling
  if (!inventory.empty()) {
      if (input_manager.isActionPressed(GameAction::NextItem)) {
          active_item_index = (active_item_index + 1) % inventory.size();
      } else if (input_manager.isActionPressed(GameAction::PrevItem)) {
          active_item_index = (active_item_index - 1 + inventory.size()) % inventory.size();
      }

      // Start using item
      if (input_manager.isActionPressed(GameAction::UseItem)) {
          if (!inventory[active_item_index]->isEmpty() && isGrounded() && !isExecuting() && !stats.isDead()) {
              item_use_timer = inventory[active_item_index]->getUseDuration();
          }
      }
  }

  if (input_manager.isActionPressed(GameAction::Attack) && gate.canAttack) {
    if (combat_component.initiateCombo(combo)) {
      if (ctx.sound_controller != nullptr) {
        ctx.sound_controller->stopSFX(AssetID::SFX_WALK);
        ctx.sound_controller->stopSFX(AssetID::SFX_RUN);
        ctx.sound_controller->playSFX(AssetID::SFX_SLASH);
      }
    }
  }
  // Latched rather than acted on, then spent on the first frame that will have
  // it -- this one, when nothing is in the way. See guard_buffer_timer.
  if (input_manager.isActionPressed(GameAction::Parry)) {
    guard_buffer_timer = GUARD_BUFFER_WINDOW;
  }
  if (guard_buffer_timer > 0.0f && gate.canGuard && combat_component.canGuard()) {
    // canGuard() asked here as well as inside startGuard(), which is otherwise
    // authoritative: a press the component would refuse has to stay in the
    // buffer for the next frame rather than be spent on a call that does
    // nothing. The one thing that must not happen is spending it and clearing
    // it in the same breath.
    guard_buffer_timer = 0.0f;

    // The button's state now, not when the press was latched. A tap released
    // during the recovery it was buffered through opens the deflect window and
    // then lapses; only a button still down becomes a held block.
    const bool held = input_manager.isActionPressed(GameAction::Parry) ||
                      input_manager.isActionHeld(GameAction::Parry);
    combat_component.startGuard(held);
  }
  if (input_manager.isActionReleased(GameAction::Parry)) {
    // Not gated: releasing a guard is letting go of a commitment, not starting
    // one. A stagger that swallowed the release would leave the guard stuck up
    // once it recovered.
    //
    // The buffer is deliberately left armed. A press and release that both land
    // inside one recovery is a tap asking for a deflect, and honouring it a few
    // frames late is the whole point of latching it.
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
    if (combat_component.startDodge(duration)) {
      animator.setDodge(dodge);
      if (ctx.sound_controller != nullptr) {
        ctx.sound_controller->stopSFX(AssetID::SFX_WALK);
        ctx.sound_controller->stopSFX(AssetID::SFX_RUN);
        ctx.sound_controller->playSFX(AssetID::SFX_DASH);
      }
    }
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
  // LockOn is handled globally in GameplayState
}

CharacterRenderData Player::getRenderData() const {
  TransformData transform;
  transform.position = position;
  transform.rotation = rotation;
  transform.scale = {1.0f, 1.0f, 1.0f};

  return {AssetID::PLAYER_WOLF, transform, animator.renderState()};
}
