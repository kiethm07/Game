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

namespace {
/// Walk.glb carries a single clip, whose name is the armature's rather than
/// anything descriptive.
const AnimStateMachine<SwordmanAnimState>::Desc kAnimTable[] = {
    /* Idle */ {"Armature|mixamo.com|Layer0", true, 1.0f, false, 0.0f},
};
} // namespace

Swordman::Swordman(Vector3 start_position) : Enemy(start_position) {
  stats = Stats(1000.0f, 100.0f, 10.0f);
  combo = {AttackID::PlayerLight1};
  stealth_component.addSensor(std::make_shared<VisionSensor>(20.0f, 70.0f));
  stealth_component.addSensor(std::make_shared<SoundSensor>(6.0f));
  stealth_component.addSensor(std::make_shared<ProximitySensor>(1.2f));
  stealth_component.addSensor(std::make_shared<CombatSenseSensor>(10.0f));
  
  spawn_position = start_position;
  spawn_yaw = 0.0f; // Could be randomized or passed in
  
  setupBehaviorTree();
}

void Swordman::update(const UpdateContext &ctx) {
  const float dt = ctx.dt;
  current_ctx = &ctx;

  stats.update(dt);

  if (ctx.assets)
    animator.resolveClips(*ctx.assets);

  const bool dead = stats.isDead();
  if (!dead) {
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
        setHorizontalVelocity({0.0f, 0.0f, 0.0f});
        if (!animator.isFlinching()) {
            animator.queueReaction(false);
        }
    }
    
    if (combat_component.getCurrentState() == CombatState::BeingExecuted) {
        setHorizontalVelocity({0.0f, 0.0f, 0.0f});
    } else if (combat_component.getCurrentState() == CombatState::PostureBroken) {
        if (!animator.isFlinching()) {
            animator.queueReaction(false);
        }
    } else {
        combat_component.update(dt);
        ai_component.update();
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
  frame.dead = dead;

  animator.update(frame, dt);
}

void Swordman::onDamaged(bool blocked, bool parried) {
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
  auto moveAlongPath = [this](float speed) {
    if (current_path.empty()) {
      this->setHorizontalVelocity({0, 0, 0});
      return NodeState::SUCCESS;
    }
    
    Vector3 target = current_path.front();
    Vector3 dir = Vector3Subtract(target, position);
    float dist = Vector2Distance({position.x, position.z}, {target.x, target.z});
    
    if (dist < 0.15f) {
      current_path.erase(current_path.begin());
      if (current_path.empty()) {
        this->setHorizontalVelocity({0, 0, 0});
        return NodeState::SUCCESS;
      }
      target = current_path.front();
      dir = Vector3Subtract(target, position);
    }
    
    Vector3 normalized_dir = Vector3Normalize({dir.x, 0.0f, dir.z});
    this->setHorizontalVelocity({normalized_dir.x * speed, 0.0f, normalized_dir.z * speed});
    
    float target_yaw = std::atan2(normalized_dir.x, normalized_dir.z) * RAD2DEG;
    float angle_diff = target_yaw - rotation.y;
    while (angle_diff < -180.0f) angle_diff += 360.0f;
    while (angle_diff > 180.0f) angle_diff -= 360.0f;
    
    float alpha = 10.0f * current_ctx->dt;
    if (alpha > 1.0f) alpha = 1.0f;
    
    rotation.y += angle_diff * alpha;
    while (rotation.y < 0.0f) rotation.y += 360.0f;
    while (rotation.y >= 360.0f) rotation.y -= 360.0f;
    
    return NodeState::RUNNING;
  };
  
  auto truncatePathBySmoke = [this](std::vector<Vector3>& path) {
      if (!current_ctx || !current_ctx->smoke_clouds) return;
      for (size_t i = 0; i < path.size() - 1; ++i) {
          Vector3 A = path[i];
          Vector3 B = path[i + 1];
          Vector3 dir = Vector3Subtract(B, A);
          float len = Vector3Length(dir);
          if (len == 0.0f) continue;
          Vector3 norm_dir = Vector3Scale(dir, 1.0f / len);
          
          for (const auto& smoke : *current_ctx->smoke_clouds) {
              if (smoke.owner == this) continue;
              
              Vector3 L = Vector3Subtract(smoke.position, A);
              float tca = Vector3DotProduct(L, norm_dir);
              if (tca < 0.0f) continue; // going away
              float d2 = Vector3DotProduct(L, L) - tca * tca;
              float r2 = smoke.radius * smoke.radius;
              if (d2 > r2) continue; // misses sphere
              float thc = std::sqrt(r2 - d2);
              float t0 = tca - thc;
              
              if (t0 > 0.0f && t0 < len) {
                  Vector3 hit_pos = Vector3Add(A, Vector3Scale(norm_dir, t0));
                  path.resize(i + 1);
                  path.push_back(hit_pos);
                  return; // Path truncated
              }
          }
      }
  };

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
  auto combatAction = std::make_shared<Action>([this, moveAlongPath, truncatePathBySmoke]() {
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
      
      // We removed the instant snap here! 
      // It will now smoothly track the player during the AttackStartup phase (handled above)
      
      combat_component.initiateCombo(combo);
      
      // Randomize cooldown and preferred distance for the next cycle
      attack_cooldown_timer = 1.5f + (rand() % 150) / 100.0f; // 1.5s to 3.0s
      float base_dist = 3.0f + (rand() % 200) / 100.0f; // 3.0m to 5.0m
      preferred_distance_min = base_dist;
      preferred_distance_max = base_dist + 0.5f;
      
      return NodeState::SUCCESS;
    }
    
    // 2. If close and on similar elevation, don't use NavMesh (prevents jitter), use direct movement
    if (distance < 5.0f && std::abs(position.y - target_pos.y) < 1.0f) {
      Vector3 move_dir = {0, 0, 0};
      float current_speed = MOVEMENT_SPEED * 0.8f;
      
      if (attack_cooldown_timer > 0.0f) {
        // Randomize strafe direction occasionally
        if (circle_timer <= 0.0f) {
            circle_direction = (rand() % 2 == 0) ? -1.0f : 1.0f;
            circle_timer = (rand() % 200 + 200) / 100.0f; // 2.0s to 4.0s
        }
        
        // Calculate tangent vector (perpendicular to player direction)
        Vector3 tangent = {-to_player_norm.z, 0.0f, to_player_norm.x};
        Vector3 strafe_dir = Vector3Scale(tangent, circle_direction);

        // On Cooldown: Maintain a randomized preferred distance while circling
        if (distance < preferred_distance_min) {
            // Too close, back away aggressively while strafing
            Vector3 back_dir = Vector3Scale(to_player_norm, -1.0f);
            move_dir = Vector3Normalize(Vector3Add(back_dir, Vector3Scale(strafe_dir, 0.5f)));
            current_speed = MOVEMENT_SPEED * 1.2f; 
        } else if (distance > preferred_distance_max) {
            // Too far, close in slightly while strafing
            move_dir = Vector3Normalize(Vector3Add(to_player_norm, Vector3Scale(strafe_dir, 0.5f)));
            current_speed = MOVEMENT_SPEED * 0.8f;
        } else {
            // Sweet spot, purely circle the player
            move_dir = strafe_dir;
            current_speed = MOVEMENT_SPEED * 0.7f;
        }
      } else {
        // Attack is ready but out of reach: move directly towards player
        move_dir = to_player_norm;
        current_speed = MOVEMENT_SPEED; // Full speed when going in for the kill
      }
      
      Vector3 current_velocity = {move_dir.x * current_speed, 0.0f, move_dir.z * current_speed};
      
      // Constrain direct movement to the NavMesh to prevent falling off cliffs
      if (current_ctx->nav_query) {
          Vector3 intended_pos = Vector3Add(position, Vector3Scale(current_velocity, current_ctx->dt));
          Vector3 constrained_pos = current_ctx->nav_query->getConstrainedPosition(position, intended_pos);
          Vector3 constrained_vel = Vector3Scale(Vector3Subtract(constrained_pos, position), 1.0f / current_ctx->dt);
          this->setHorizontalVelocity(constrained_vel);
      } else {
          this->setHorizontalVelocity(current_velocity);
      }
      
      // ALWAYS keep eye contact with the player during close combat
      float target_yaw = std::atan2(to_player_norm.x, to_player_norm.z) * RAD2DEG;
      float angle_diff = target_yaw - rotation.y;
      while (angle_diff < -180.0f) angle_diff += 360.0f;
      while (angle_diff > 180.0f) angle_diff -= 360.0f;
      rotation.y += angle_diff * (10.0f * current_ctx->dt);
      
      return NodeState::RUNNING;
    }
    
    // 3. If far, use NavMesh to chase
    path_recalc_timer -= current_ctx->dt;
    if (path_recalc_timer <= 0.0f) {
      if (current_ctx->nav_query) {
          // Add a small deterministic offset based on enemy ID to prevent them from all aligning in a single straight line
          Vector3 target_pos = stealth_component.getLastKnownPlayerPos();
          Vector3 offset = { std::sin(getId() * 1.5f) * 1.5f, 0.0f, std::cos(getId() * 1.5f) * 1.5f };
          current_path = current_ctx->nav_query->findPath(position, Vector3Add(target_pos, offset));
          truncatePathBySmoke(current_path);
      }
      path_recalc_timer = 0.25f;
    }
    
    moveAlongPath(MOVEMENT_SPEED);
    return NodeState::RUNNING;
  });

  auto awareSequence = std::make_shared<Sequence>(std::vector<NodePtr>{
    isAware,
    combatAction
  });

  // ---------------------------------------------------------
  // Suspicious Node (Investigation)
  // ---------------------------------------------------------
  auto investigateAction = std::make_shared<Action>([this, moveAlongPath, truncatePathBySmoke]() {
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
      moveAlongPath(MOVEMENT_SPEED * 0.6f); // Walk slowly
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
  auto returnToPostAction = std::make_shared<Action>([this, moveAlongPath, truncatePathBySmoke]() {
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
      moveAlongPath(MOVEMENT_SPEED * 0.5f); // Walk slowly back
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

std::vector<HitBox> Swordman::getActiveHitBoxes() const {
  if (stats.isDead())
    return {};

  std::vector<HitBox> active_hitboxes;

  if (combat_component.getCurrentState() == CombatState::AttackActive) {
    const AttackData* active_attack = combat_component.getActiveAttack();
    if (!active_attack) return active_hitboxes;

    float yaw_rad = rotation.y * DEG2RAD;
    Vector3 forward = {std::sin(yaw_rad), 0.0f, std::cos(yaw_rad)};
    Vector3 right = {-std::cos(yaw_rad), 0.0f, std::sin(yaw_rad)};
    Vector3 up = {0.0f, 1.0f, 0.0f};

    for (const auto& def : active_attack->getHitBoxDefs()) {
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

  return {AssetID::ENEMY_ASHIGARU, transform, animator.renderState(), dissolveProgress, decay_type};
}