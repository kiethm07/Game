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
  // model carries the clips they name -- `Attack_H`, `Combo_3` and `Combo_2`
  // exist in the greatsword pack and nowhere else. Anything on the player's
  // borrowed set keeps the single light swing it always had.
  if (asset == AssetID::ENEMY_MINIBOSS) {
    attack_pattern = {Combo{AttackID::MiniBossSwing},
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
  } else {
    attack_pattern = {Combo{AttackID::PlayerLight1}};
  }
  stealth_component.addSensor(std::make_shared<VisionSensor>(
      o.visionRadius.value_or(20.0f), o.visionConeDegrees.value_or(70.0f)));
  stealth_component.addSensor(std::make_shared<SoundSensor>(6.0f));
  stealth_component.addSensor(std::make_shared<ProximitySensor>(1.2f));
  stealth_component.addSensor(std::make_shared<CombatSenseSensor>(10.0f));

  sword_trail.setColors({255, 230, 200, 240}, {255, 70, 40, 200});

  
  // Stagger initial waiting time so they don't all attack at the exact same moment
  attack_cooldown_timer = (getId() % 4) * 0.8f + ((rand() % 100) / 100.0f);
  
  setupBehaviorTree();
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
      combat_component.initiateCombo(next);
      
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
        Vector3 target_vel = {move_dir.x * run_speed, 0.0f, move_dir.z * run_speed};
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
    
    moveAlongPath(run_speed);
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
    
    if (dist_to_post > 1.0f) {
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