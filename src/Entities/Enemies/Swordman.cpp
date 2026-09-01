#include "AI/NavMeshQuery.h"
#include "Entities/Character.h"
#include <Entities/Enemies/Swordman.h>
#include <cstdlib>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>
#include <AI/NavMeshQuery.h>
#include <Stealth/CombatSenseSensor.h>
#include <GameManager/SmokeCloud.h>
#include <GameManager/SoundController.h>
#include <Rendering/BoneSocketHelper.h>

namespace {
/// Walk.glb carries a single clip, whose name is the armature's rather than
/// anything descriptive.
// The single-clip kAnimTable that stood here had zero uses -- it was
// superseded by SwordmanAnimator::descTable() and never removed.
} // namespace

Swordman::Swordman(const EnemySpawn &spawn, AssetID asset)
    : Enemy(spawn), animator(asset) {
  // Every literal below is this type's default, and the only place it lives.
  // An override that the overlay left out resolves back to it through value_or,
  // so retuning a swordman is an edit here and nowhere else -- not in the
  // loader, not in the schema, not in EnemyOverrides.
  const EnemyOverrides &o = spawn.overrides;

  stats = Stats(o.maxHealth.value_or(1000.0f), 100.0f, 10.0f);

  // The rotation this enemy walks through: normal, first combo, second combo,
  // back to normal. Keyed on the ASSET rather than on the spawn's EnemyType,
  // because what decides whether these attacks can be played is whether the
  // model carries the clips they name -- `Attack_Spin`, `Combo_3` and `Combo_2`
  // exist in the greatsword pack and nowhere else, and `Attack_Rapid` and
  // `Attack_Jump` only in the Mutant pack. Anything on the player's borrowed
  // set keeps the single light swing it always had.
  if (asset == AssetID::ENEMY_FINALBOSS) {
    attack_pattern = {Combo{AttackID::FinalBossPunch},
                      Combo{AttackID::FinalBossFlurry},
                      Combo{AttackID::FinalBossLeap}};

    // The boss is 2.792 m -- tools/scale_finalboss.py takes the rig to 1.5x as
    // the last step before export -- so its hurtbox and head marker scale with
    // it. Left at Enemy's 2.0/0.5 defaults the capsule would cover only the
    // bottom two thirds of the model: the head would not be hittable and the
    // posture bar would float inside the chest.
    body_height = 3.0f;
    body_radius = 0.75f;

    // The Mutant pack's Walk is authored at 1.83 m/s (2.596 m over its 85
    // playable frames at 60 Hz) and its Run at 3.31, so SwordmanAnimator takes
    // Run over Walk above 2.93 -- RUN_SPEED_FACTOR x the walk. Both speeds are
    // set against those two numbers rather than copied from the miniboss, whose
    // pack is authored differently again:
    //   * 2.55 keeps the approach under the threshold and plays the walk at
    //     1.39x, and circling (walk_speed x 0.8 = 2.04) at 1.11x.
    //   * 5.70 plays the run at 1.72x, which is a charge without being the
    //     slow-motion sprint that picking a speed off the wrong pack produces.
    //
    // Both are the pre-scale values times the same 1.5 the rig grew by. That is
    // not a coincidence to be tidied away: the clips' authored speeds scaled
    // with the rig, so scaling the character speeds by the same factor is what
    // keeps every clip playing at the rate it was tuned to.
    walk_speed = 2.55f;
    run_speed = 5.7f;

    // No gait switch, for the same reason as the miniboss: the charge is meant
    // to stay a charge all the way in.
    gait_switch_distance = 0.0f;
  } else if (asset == AssetID::ENEMY_MINIBOSS) {
    attack_pattern = {Combo{AttackID::MiniBossSpin},
                      Combo{AttackID::MiniBossDoubleSwing},
                      Combo{AttackID::MiniBossTripleSwing}};

    // The greatsword pack's Walk is authored at 1.06 m/s and its Run at 4.05,
    // so SwordmanAnimator switches to the run above 1.70 (RUN_SPEED_FACTOR x
    // the walk). The old single 2.0 sat just the wrong side of that line: it
    // selected the run and then played it at 2.00/4.05 = 0.49x, a sprint in
    // slow motion. Circling stays where it was -- walk_speed x 0.8 = 1.6, under
    // the threshold, the walk cycle at 1.5x -- and the charge is now 6.5, which
    // plays the run at 1.6x and is a shade quicker than the player's own walk,
    // so the boss closes on anything but a sprint.
    walk_speed = 2.0f;
    run_speed = 6.5f;

    // No gait switch for the boss. The 6.5 above is a charge that is meant to
    // stay a charge all the way in; dropping it to a walk at the hearing radius
    // would take the pressure off exactly where the fight starts.
    gait_switch_distance = 0.0f;
  } else if (asset == AssetID::ENEMY_KIMONO) {
    attack_pattern = {Combo{AttackID::KimonoSwing},
                      Combo{AttackID::KimonoCleave},
                      Combo{AttackID::KimonoLunge}};

    // Mini-boss tier, and set here rather than left to an enemies.json override
    // so the type carries its own difficulty: a spawn that names nothing still
    // gets the boss, not a swordman wearing its model. More posture and a
    // slower rebuild than a mook, because the whole fight is the posture bar.
    stats = Stats(o.maxHealth.value_or(1600.0f), 140.0f, 8.0f);

    // 1.65 m -- the shortest character in the game, against the player's 1.8
    // and the mini boss's 2.63. Left at Enemy's 2.0/0.5 defaults the capsule
    // would stand a head taller than the model, so shots over the shoulder
    // would connect and the posture bar would float clear of the head.
    body_height = 1.8f;
    body_radius = 0.45f;

    // The greatsword pack authors Walk at 1.09 m/s and Run at 4.03, and
    // tools/retarget_kimono.py scaled every clip's hip travel by 0.9204 (this
    // character's hips sit at 0.880 against the clip rig's 0.956), so on THIS
    // rig they are 1.00 and 3.71. SwordmanAnimator takes Run over Walk above
    // 1.60 -- RUN_SPEED_FACTOR x the walk -- and both numbers are set against
    // those, not copied from the mini boss, whose pack is authored at a
    // different scale again:
    //   * 1.40 stays under the threshold and plays the walk at 1.40x, and
    //     circling (walk_speed x 0.8 = 1.12) at 1.12x. That is the brief's
    //     "slow intimidating forward advance" and not a scurry.
    //   * 4.80 plays the run at 1.29x. Quicker than the player's walk (1.85),
    //     so backing away does not work, and slower than their sprint (7.38),
    //     so committing to a sprint still breaks contact.
    walk_speed = 1.4f;
    run_speed = 4.8f;

    // Unlike the other two bosses this one KEEPS the gait switch. Both of those
    // charge from any distance on purpose; this character's brief is a measured
    // advance, and the switch is what gives it one -- a run to close, dropping
    // to that 1.40 walk inside the hearing radius.
  } else {
    attack_pattern = {Combo{AttackID::PlayerLight1}};

    // The player's borrowed pack authors Walk at 1.36 m/s and Run at 3.69, so
    // SwordmanAnimator takes Run over Walk above 2.18 (RUN_SPEED_FACTOR x the
    // walk). run_speed defaulted to 2.0 -- just under that line -- so an
    // approach selected the WALK clip and merely time-scaled it to 1.47x, which
    // is why this enemy strolled at the player from across the map however far
    // away it was. 5.0 clears the threshold, plays the run at a 1.35x jog, and
    // sits inside the band the mini boss is tuned against: quicker than the
    // player's walk (1.85) so walking away does not work, slower than their
    // sprint (7.38) so sprinting away does.
    run_speed = 5.0f;
  }
  stealth_component.addSensor(std::make_shared<VisionSensor>(
      o.visionRadius.value_or(20.0f), o.visionConeDegrees.value_or(70.0f)));
  stealth_component.addSensor(std::make_shared<SoundSensor>(HEARING_RADIUS));
  stealth_component.addSensor(std::make_shared<ProximitySensor>(1.2f));
  stealth_component.addSensor(std::make_shared<CombatSenseSensor>(10.0f));

  sword_trail.setColors({255, 230, 200, 240}, {255, 70, 40, 200});

  
  // Stagger initial waiting time so they don't all attack at the exact same moment
  attack_cooldown_timer = (getId() % 4) * 0.8f + ((rand() % 100) / 100.0f);
  
  setupBehaviorTree();
}

/// Run when the player is outside the hearing radius, walk once inside it.
///
/// The switch is latched rather than a bare comparison. Both characters are
/// moving, so the distance crosses the boundary repeatedly at walking pace, and
/// an unlatched test would alternate walk_speed and run_speed frame to frame --
/// which SwordmanAnimator reads as alternating CLIPS, restarting the 0.15 s
/// blend before it can ever finish and leaving the enemy twitching on the spot.
float Swordman::chaseSpeed(float distance) {
  if (gait_switch_distance <= 0.0f) return run_speed;

  if (chase_running) {
    if (distance < gait_switch_distance - GAIT_SWITCH_HYSTERESIS)
      chase_running = false;
  } else if (distance > gait_switch_distance + GAIT_SWITCH_HYSTERESIS) {
    chase_running = true;
  }
  return chase_running ? run_speed : walk_speed;
}

void Swordman::update(const UpdateContext &ctx) {
  const float dt = ctx.dt;
  current_ctx = &ctx;

  stats.update(dt);
  sword_trail.update(dt);

  if (ctx.assets)
    animator.resolveClips(*ctx.assets);

  const bool dead = stats.isDead();
  if (!dead) {
    if (combat_component.getCurrentState() == CombatState::BeingExecuted) {
        setHorizontalVelocity({0.0f, 0.0f, 0.0f});
        sword_trail.clear();
    } else {
        bool in_smoke = false;
        if (ctx.smoke_clouds) {
            for (const auto& smoke : *ctx.smoke_clouds) {
                if (smoke.owner == this) continue;
                if (Vector3DistanceSqr(position, smoke.position) <= smoke.radius * smoke.radius) {
                    in_smoke = true;
                    break;
                }
            }
        }

        if (in_smoke) {
            stealth_component.blind();
            combat_component.interrupt();
            sword_trail.clear();
            setHorizontalVelocity({0.0f, 0.0f, 0.0f});
            if (!animator.isFlinching()) {
                animator.queueReaction(false);
            }
        }

        bool was_posture_broken = (combat_component.getCurrentState() == CombatState::PostureBroken);
        
        combat_component.update(dt);
        
        if (combat_component.getCurrentState() == CombatState::PostureBroken) {
            sword_trail.clear();
            if (!animator.isFlinching()) {
                animator.queueReaction(false);
            }
        } else {
            if (was_posture_broken) {
                stats.resetPosture();
            }
            ai_component.update();
        }
    }
  }
  if (combat_component.getCurrentState() == CombatState::Idle) {
    if (attack_cooldown_timer > 0.0f) attack_cooldown_timer -= dt;
  }
  if (move_cooldown_timer > 0.0f) move_cooldown_timer -= dt;
  if (investigation_timer > 0.0f) investigation_timer -= dt;
  if (circle_timer > 0.0f) circle_timer -= dt;


  if (combat_component.getCurrentState() == CombatState::PostureBroken)
    setHorizontalVelocity({0.0f, 0.0f, 0.0f});

  animator.updateFlinch(dt, ctx.assets);

  SwordmanAnimator::Frame frame;
  frame.combat = &combat_component;
  frame.assets = ctx.assets;
  // Read from the same field PhysicsManager integrates, so the stride matches
  // the travel exactly.
  const Vector3 velocity = getHorizontalVelocity();
  frame.moving = (velocity.x != 0.0f || velocity.z != 0.0f);
  frame.speed = Vector3Length(velocity);
  frame.dead = dead;

  updateStrafing(velocity, in_direct_combat);
  frame.strafing = isStrafing();
  frame.localMoveDir = getLocalMoveDir();
  frame.grounded = isGrounded();

  animator.update(frame, dt);
  applyAttackRootMotion(dt);

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

void Swordman::applyAttackAdvance(float dt, const AttackData &attack) {
  // Re-aimed every frame, which is the whole point: this is for an attack long
  // enough that a player can walk out of it while it runs, so a direction
  // chosen once at the wind-up would be aimed at where they used to be. It is
  // the same target the behaviour tree tracks with during an attack's start-up.
  const Vector3 target = stealth_component.getLastKnownPlayerPos();
  Vector3 to_target = {target.x - position.x, 0.0f, target.z - position.z};
  const float distance = Vector3Length(to_target);

  if (distance > 0.01f) {
    const float target_yaw = std::atan2(to_target.x, to_target.z) * RAD2DEG;
    float angle_diff = target_yaw - rotation.y;
    while (angle_diff < -180.0f) angle_diff += 360.0f;
    while (angle_diff > 180.0f) angle_diff -= 360.0f;
    // Rate limited rather than snapped. The hitbox is placed in this
    // character's own frame, so turning the character turns the pass with it --
    // which is what keeps the blade coming round onto a player who repositions,
    // and also what would teleport it onto them if the turn were instant.
    const float step = attack.getAdvanceTurnRate() * dt;
    rotation.y += std::max(-step, std::min(step, angle_diff));
    while (rotation.y < 0.0f) rotation.y += 360.0f;
    while (rotation.y >= 360.0f) rotation.y -= 360.0f;
  }

  Vector3 velocity = {0.0f, 0.0f, 0.0f};
  const float stop = attack.getAdvanceStopDistance();
  if (distance > stop && distance > 0.01f) {
    // Capped so one frame cannot carry the boss past the distance it is closing
    // to and start shoving the player around the arena.
    const float speed = std::min(attack.getAdvanceSpeed(), (distance - stop) / dt);
    velocity = Vector3Scale(to_target, speed / distance);
  }
  setHorizontalVelocity(velocity);
}

bool Swordman::attackIsSwinging() const {
  // Between the first hit window opening and the last one closing, the gaps
  // between them included -- those are the state machine back in a wind-up
  // while the blade is still going round, and treating them as "not swinging"
  // would stall the chase twice mid-spin.
  const CombatState state = combat_component.getCurrentState();
  if (state == CombatState::AttackActive) return true;
  return state == CombatState::AttackStartup && combat_component.getActiveSwing() > 0;
}

void Swordman::applyAttackRootMotion(float dt) {
  // Enemy attacks were in place by construction until this existed: the
  // animator threw away the track apply() handed back, so no enemy clip's
  // authored travel could reach the character controller even though
  // AttackData has carried a usesRootMotion() flag all along. Only the player
  // consumed it. The final boss's `Attack_Jump` leaps 1.704 m, and without this
  // it would take off, land on the spot it started from, and snap the mesh back
  // when the renderer cancelled the travel it could not consume.
  // Gated on the ANIMATION, not on the combat state alone. A flinch or a
  // posture break outranks the swing in the animator's ladder, so the combat
  // machine can still be in an attack phase while something else is on screen
  // -- and a clip that is not playing must not drive the character.
  const AttackData *attack = combat_component.getActiveAttack();
  const RootMotion::Track *track = animator.activeTrack();
  const bool playing = dt > 0.0f && animator.playingAttack() && attack != nullptr;
  const bool driving = playing && attack->usesRootMotion() &&
                       track != nullptr && track->hasMotion;
  // The other way an attack moves: not the clip's authored travel but a live
  // chase, for the swings that ask for one. Same ownership of the velocity and
  // the same release below, because from the controller's side the two are the
  // same thing -- something other than the behaviour tree deciding where this
  // character goes this frame.
  const bool closing = playing && !driving &&
                       attack->getAdvanceSpeed() > 0.0f && attackIsSwinging();

  if (closing) {
    root_motion_driving = true;
    applyAttackAdvance(dt, *attack);
    return;
  }

  if (!driving) {
    // Release what we took. While driving, this function OWNS the horizontal
    // velocity -- it overwrites whatever the behavior tree wrote earlier in the
    // frame -- so when it stops driving, the last value it wrote is still in
    // there and nothing else is obliged to clear it. An attack cut short
    // mid-leap by a flinch or a posture break would otherwise coast on its last
    // root velocity until the AI happened to write a new one. Player::update
    // pins velocity to zero in the same situation and for the same reason.
    if (root_motion_driving) {
      setHorizontalVelocity({0.0f, 0.0f, 0.0f});
      root_motion_driving = false;
    }
    return;
  }
  root_motion_driving = true;

  // Expressed as a velocity rather than a position offset, exactly as
  // Player::applyRootMotion does it, so the travel flows through
  // PhysicsManager's depenetration and ground snapping. Writing the position
  // here would let a leaping boss pass through a wall.
  const Vector3 local = animator.sampleRootDelta(*track);
  const Vector3 world = RootMotion::toWorld(local, rotation.y);
  Vector3 velocity = Vector3Scale(world, 1.0f / dt);

  // A dt spike (a breakpoint, a dragged window) would otherwise turn one
  // frame's travel into an arbitrarily large velocity and tunnel the boss
  // through the level.
  const float speed = Vector3Length(velocity);
  if (speed > MAX_ATTACK_ROOT_SPEED)
    velocity = Vector3Scale(velocity, MAX_ATTACK_ROOT_SPEED / speed);

  setHorizontalVelocity({velocity.x, 0.0f, velocity.z});
}

void Swordman::onDamaged(bool blocked, bool parried) {
  sword_trail.clear();
  if (parried) {
    attack_cooldown_timer = std::max(attack_cooldown_timer, 0.8f);
    move_cooldown_timer = std::max(move_cooldown_timer, 0.5f);
  } else {
    animator.queueReaction(blocked);
  }
}

void Swordman::setupBehaviorTree() {
  using namespace BT;

  // ---------------------------------------------------------
  // Common Helper: Move along a path
  // ---------------------------------------------------------
  

  // ---------------------------------------------------------
  // Conditions
  // ---------------------------------------------------------
  auto isAware = std::make_shared<Condition>([this]() {
    return stealth_component.getStealthState() == StealthState::Aware;
  });

  auto isSuspicious = std::make_shared<Condition>([this]() {
    return stealth_component.getStealthState() == StealthState::Suspicious;
  });

  auto isUnaware = std::make_shared<Condition>([this]() {
    return stealth_component.getStealthState() == StealthState::Unaware;
  });

  // ---------------------------------------------------------
  // Aware Node (Combat)
  // ---------------------------------------------------------
  auto combatAction = std::make_shared<Action>([this]() {
    if (!current_ctx) return NodeState::FAILURE;
    if (combat_component.getCurrentState() == CombatState::PostureBroken) return NodeState::SUCCESS;
    
    // 1. If currently attacking, parrying, STAGGERED (flinching), or under move cooldown, don't interrupt with movement/new attacks!
    if (combat_component.getCurrentState() != CombatState::Idle || animator.isFlinching() || move_cooldown_timer > 0.0f) {
      this->setHorizontalVelocity({0, 0, 0});
      
      // Allow smooth tracking during the wind-up/startup phase of an attack, or while in move cooldown
      if (combat_component.getCurrentState() == CombatState::AttackStartup || move_cooldown_timer > 0.0f) {
        Vector3 target_pos = stealth_component.getLastKnownPlayerPos();
        Vector3 to_player = Vector3Subtract(target_pos, position);
        float target_yaw = std::atan2(to_player.x, to_player.z) * RAD2DEG;
        float angle_diff = target_yaw - rotation.y;
        while (angle_diff < -180.0f) angle_diff += 360.0f;
        while (angle_diff > 180.0f) angle_diff -= 360.0f;
        rotation.y += angle_diff * (15.0f * current_ctx->dt);
        while (rotation.y < 0.0f) rotation.y += 360.0f;
        while (rotation.y >= 360.0f) rotation.y -= 360.0f;
      }
      
      return NodeState::RUNNING; // Currently attacking or staggered
    }
    
    Vector3 target_pos = stealth_component.getLastKnownPlayerPos();
    float distance = Vector3Distance(position, target_pos);
    Vector3 to_player = Vector3Subtract(target_pos, position);
    to_player.y = 0.0f;
    Vector3 to_player_norm = Vector3Normalize(to_player);
    
    // 1. If in reach and attack is ready -> Attack!
    // Enemy walks up to 1.8m to swing, then backs out to 4.0m on cooldown!
    if (distance < 1.8f && attack_cooldown_timer <= 0.0f) {
      this->setHorizontalVelocity({0, 0, 0});

      // Next in the rotation, then advance. The reference outlives this call:
      // `attack_pattern` is fixed at construction, which is what makes it safe
      // to hand initiateCombo a pointer into it.
      const Combo &next = attack_pattern[next_attack];
      next_attack = (next_attack + 1) % attack_pattern.size();
      if (combat_component.initiateCombo(next)) {
        if (current_ctx != nullptr && current_ctx->sound_controller != nullptr) {
          current_ctx->sound_controller->playSFX(AssetID::SFX_SLASH);
        }
      }
      
      // Randomize cooldown completely for each turn (1.5s to 3.5s)
      attack_cooldown_timer = 1.5f + (rand() % 200) / 100.0f;
      float base_dist = 3.0f + (rand() % 150) / 100.0f; // 3.0m to 4.5m
      preferred_distance_min = base_dist;
      preferred_distance_max = base_dist + 1.0f;
      
      return NodeState::SUCCESS;
    }
    
    // 2. If close and on similar elevation, don't use NavMesh (prevents jitter), use direct movement
    // Threshold must be larger than preferred_distance_max (which can be up to 8.0m) to prevent boundary vibration
    // Vertical threshold increased to 3.0f to allow smooth direct combat on ramps without flip-flopping to NavMesh
    bool has_nav_los = false;
    if (current_ctx->nav_query != nullptr) {
      has_nav_los = current_ctx->nav_query->raycast(position, target_pos);
    }
    
    // Only use direct movement if there are NO gaps or walls between enemy and player
    if (distance < 10.0f && std::abs(position.y - target_pos.y) < 3.0f && has_nav_los) {
      if (attack_cooldown_timer > 0.0f) {
        updateCombatCircling(*current_ctx, target_pos, walk_speed * 0.8f);
      } else {
        // Attack is ready: move directly towards player to get into attack reach
        //
        // Not strafing, despite this being direct combat. `in_direct_combat` is
        // what puts the animator on the strafe set, and the strafe set has no
        // run in it: a head-on charge resolves to StrafeForward, which is the
        // WALK clip time-scaled to whatever the charge speed is. That is why
        // the mini boss only ever walked -- the Run rung is unreachable while
        // this flag is up, however fast the character is actually travelling.
        // Sidestepping is for the circle; closing the distance is a run.
        in_direct_combat = false;
        Vector3 move_dir = to_player_norm;
        const float approach_speed = chaseSpeed(distance);
        Vector3 target_vel = {move_dir.x * approach_speed, 0.0f,
                              move_dir.z * approach_speed};
        float lerp_alpha = 1.0f - std::exp(-15.0f * current_ctx->dt);
        Vector3 old_vel = this->getHorizontalVelocity();
        Vector3 smoothed_vel = Vector3Lerp(old_vel, target_vel, lerp_alpha);
        this->setHorizontalVelocity(smoothed_vel);

        float target_yaw = std::atan2(to_player_norm.x, to_player_norm.z) * RAD2DEG;
        float angle_diff = target_yaw - rotation.y;
        while (angle_diff < -180.0f) angle_diff += 360.0f;
        while (angle_diff > 180.0f) angle_diff -= 360.0f;
        float rot_alpha = 1.0f - std::exp(-18.0f * current_ctx->dt);
        rotation.y += angle_diff * rot_alpha;
        while (rotation.y < 0.0f) rotation.y += 360.0f;
        while (rotation.y >= 360.0f) rotation.y -= 360.0f;
      }
      return NodeState::RUNNING;
    }
    
    in_direct_combat = false;
    
    // 3. If far, use NavMesh to chase
    path_recalc_timer -= current_ctx->dt;
    if (path_recalc_timer <= 0.0f) {
      if (current_ctx->nav_query) {
        Vector3 target_pos = stealth_component.getLastKnownPlayerPos();
        current_path = current_ctx->nav_query->findPath(position, target_pos);
        truncatePathBySmoke(current_path);
      }
      path_recalc_timer = 0.25f;
    }
    
    moveAlongPath(chaseSpeed(distance));
    return NodeState::RUNNING;
  });

  auto awareSequence = std::make_shared<Sequence>(std::vector<NodePtr>{
    isAware,
    combatAction
  });

  // ---------------------------------------------------------
  // Suspicious Node (Investigation)
  // ---------------------------------------------------------
  auto investigateAction = std::make_shared<Action>([this]() {
    if (!current_ctx) return NodeState::FAILURE;
    
    Vector3 target = stealth_component.getLastKnownPlayerPos();
    float dist_to_target = Vector2Distance({position.x, position.z}, {target.x, target.z});
    
    if (dist_to_target > 1.0f && investigation_timer <= 0.0f) {
      // Pathfind to last known position
      path_recalc_timer -= current_ctx->dt;
      if (path_recalc_timer <= 0.0f) {
        if (current_ctx->nav_query) {
            current_path = current_ctx->nav_query->findPath(position, target);
            truncatePathBySmoke(current_path);
        }
        path_recalc_timer = 1.0f; // Recalculate less often when investigating
      }
      moveAlongPath(walk_speed * 0.6f); // Walk slowly
    } else {
      // Arrived at target, look around
      if (investigation_timer <= 0.0f) {
          investigation_timer = 4.0f; // Look around for 4 seconds
      }
      this->setHorizontalVelocity({0, 0, 0});
      
      // Slowly rotate back and forth
      float sweep = std::sin(investigation_timer * 2.0f) * 60.0f; // Sweep 60 degrees
      rotation.y += sweep * current_ctx->dt;
    }
    return NodeState::RUNNING;
  });

  auto suspiciousSequence = std::make_shared<Sequence>(std::vector<NodePtr>{
    isSuspicious,
    investigateAction
  });

  // ---------------------------------------------------------
  // Unaware Node (Return to Post / Idle)
  // ---------------------------------------------------------
  auto returnToPostAction = std::make_shared<Action>([this]() {
    if (!current_ctx) return NodeState::FAILURE;
    
    float dist_to_post = Vector2Distance({position.x, position.z}, {spawn_position.x, spawn_position.z});

    // A wanderer is home anywhere inside its disc, not just on the exact spawn
    // point. Without this the two behaviours fight: every leg of a wander ends
    // more than a metre from the post, so the return-home branch would drag it
    // straight back and it would never dwell anywhere but the centre. Falls
    // back to the literal 1.0 for the standing case, which is unchanged.
    const float post_arrival_distance = std::max(1.0f, wander_radius);

    if (dist_to_post > post_arrival_distance) {
      // Whatever leg was in progress is stale -- it was chosen from a position
      // this enemy has since left. Re-pick on arrival.
      wander_walking = false;

      // Pathfind to spawn
      path_recalc_timer -= current_ctx->dt;
      if (path_recalc_timer <= 0.0f) {
        if (current_ctx->nav_query) {
            current_path = current_ctx->nav_query->findPath(position, spawn_position);
            truncatePathBySmoke(current_path);
        }
        path_recalc_timer = 1.0f;
      }
      moveAlongPath(walk_speed * 0.5f); // Walk slowly back
    } else if (wander_radius > 0.0f) {
      // Loiter instead of standing. `in_direct_combat` is the animator's strafe
      // flag and has no business being up out of combat -- a wander that
      // inherited it from an interrupted fight would sidestep its way around
      // the post on the strafe clips.
      in_direct_combat = false;
      wanderAroundPost(walk_speed * 0.5f);
    } else {
      // At post, align to spawn yaw
      this->setHorizontalVelocity({0, 0, 0});
      float angle_diff = spawn_yaw - rotation.y;
      while (angle_diff < -180.0f) angle_diff += 360.0f;
      while (angle_diff > 180.0f) angle_diff -= 360.0f;
      
      if (std::abs(angle_diff) > 1.0f) {
          rotation.y += angle_diff * (5.0f * current_ctx->dt);
      }
    }
    return NodeState::SUCCESS; // Returning success allows root to tick again smoothly
  });

  auto unawareSequence = std::make_shared<Sequence>(std::vector<NodePtr>{
    isUnaware,
    returnToPostAction
  });

  // ---------------------------------------------------------
  // Root Selector
  // ---------------------------------------------------------
  auto rootSelector = std::make_shared<Selector>(std::vector<NodePtr>{
    awareSequence,
    suspiciousSequence,
    unawareSequence
  });

  ai_component.setRoot(rootSelector);
}


// void Swordman::takeDamage(float health_damage, float posture_damage) {
//     if (combat_component.getCurrentState() == CombatState::Parrying) return;
//     if (combat_component.getCurrentState() == CombatState::Blocking)
//     health_damage = 0.0f;

//     stats.applyDamage(health_damage, posture_damage);

//     // if (stats.isDead()) {
//     //     combat_component.resetToIdle();
//     // }
// }

CharacterRenderData Swordman::getRenderData() const {
  TransformData transform;
  transform.position = position;
  transform.rotation = rotation;
  transform.scale = visual_size;

  float dissolveProgress = 0.0f;
  if (killed_by_stealth) {
      dissolveProgress = std::fmin(dissolve_timer / 2.0f, 1.0f);
  }

  return {animator.assetId(), transform, animator.renderState(), dissolveProgress, decay_type};
}