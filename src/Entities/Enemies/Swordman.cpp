#include "Entities/Character.h"
#include <Entities/Enemies/Swordman.h>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>

Swordman::Swordman(Vector3 start_position) : Enemy(start_position) {
  stats = Stats(1000.0f, 100.0f, 15.0f);
  combo = {AttackID::PlayerLight1};
  stealth_component.addSensor(std::make_shared<VisionSensor>(20.0f, 70.0f));
  stealth_component.addSensor(std::make_shared<SoundSensor>(6.0f));
  stealth_component.addSensor(std::make_shared<ProximitySensor>(1.2f));
  
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

  combat_component.update(dt);

  if (attack_cooldown_timer > 0.0f) attack_cooldown_timer -= dt;
  if (investigation_timer > 0.0f) investigation_timer -= dt;
  if (circle_timer > 0.0f) circle_timer -= dt;

  ai_component.update();

  if (combat_component.getCurrentState() == CombatState::PostureBroken)
    setHorizontalVelocity({0.0f, 0.0f, 0.0f});

  animator.updateFlinch(dt, ctx.assets);

  // Resolved by name rather than by assuming an index. The model carries
  // exactly one clip, so a name the export toolchain has renamed still leaves
  // index 0 as the only possibility worth falling back to.
  if (ctx.assets && anim.resolveClips(*ctx.assets) &&
      anim.clipFor(SwordmanAnimState::Idle) < 0)
    anim.setClip(SwordmanAnimState::Idle, 0);

  // Advancing playback here is what the original code omitted, which left
  // enemies rendering frozen on frame 0.
  anim.apply(ctx.assets, dt, anim.select(SwordmanAnimState::Idle));
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
  auto combatAction = std::make_shared<Action>([this, moveAlongPath]() {
    if (!current_ctx) return NodeState::FAILURE;
    if (combat_component.getCurrentState() == CombatState::PostureBroken) return NodeState::SUCCESS;
    if (combat_component.getCurrentState() != CombatState::Idle) {
      this->setHorizontalVelocity({0, 0, 0});
      return NodeState::RUNNING; // Currently attacking/staggered
    }
    
    float distance = Vector3Distance(position, current_ctx->playerPos);
    Vector3 to_player = Vector3Subtract(current_ctx->playerPos, position);
    to_player.y = 0.0f;
    Vector3 to_player_norm = Vector3Normalize(to_player);
    
    // 1. If in reach and attack is ready -> Attack!
    // Enemy walks up to 1.8m to swing, then backs out to 4.0m on cooldown!
    if (distance < 1.8f && attack_cooldown_timer <= 0.0f) {
      this->setHorizontalVelocity({0, 0, 0});
      
      // Face player instantly for attack
      rotation.y = std::atan2(to_player_norm.x, to_player_norm.z) * RAD2DEG;
      
      combat_component.initiateCombo(combo);
      attack_cooldown_timer = 2.0f; // 2.0s cooldown
      return NodeState::SUCCESS;
    }
    
    // 2. If close, don't use NavMesh (prevents jitter), use direct movement
    if (distance < 5.0f) {
      Vector3 move_dir = {0, 0, 0};
      float current_speed = MOVEMENT_SPEED * 0.8f;
      
      if (attack_cooldown_timer > 0.0f) {
        // On Cooldown: Maintain a very large distance (around 3.5m - 4.0m) straight back
        if (distance < 3.5f) {
            // Too close, back away aggressively in a straight line
            move_dir = Vector3Scale(to_player_norm, -1.0f);
            current_speed = MOVEMENT_SPEED * 1.2f; 
        } else if (distance > 4.0f) {
            // Too far, close in slightly straight forward
            move_dir = to_player_norm;
            current_speed = MOVEMENT_SPEED * 0.8f;
        } else {
            // Sweet spot, just hold ground
            move_dir = {0, 0, 0};
        }
      } else {
        // Attack is ready but out of reach: move directly towards player
        move_dir = to_player_norm;
        current_speed = MOVEMENT_SPEED; // Full speed when going in for the kill
      }
      
      this->setHorizontalVelocity({move_dir.x * current_speed, 0.0f, move_dir.z * current_speed});
      
      // ALWAYS keep eye contact with the player during close combat
      float target_yaw = std::atan2(to_player_norm.x, to_player_norm.z) * RAD2DEG;
      float angle_diff = target_yaw - rotation.y;
      while (angle_diff < -180.0f) angle_diff += 360.0f;
      while (angle_diff > 180.0f) angle_diff -= 360.0f;
      rotation.y += angle_diff * (10.0f * current_ctx->dt);
      
      return NodeState::RUNNING;
    }
    
    // 3. If far (distance >= 3.0f), use NavMesh to chase
    path_recalc_timer -= current_ctx->dt;
    if (path_recalc_timer <= 0.0f) {
      if (current_ctx->nav_query) {
          current_path = current_ctx->nav_query->findPath(position, current_ctx->playerPos);
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
  auto investigateAction = std::make_shared<Action>([this, moveAlongPath]() {
    if (!current_ctx) return NodeState::FAILURE;
    
    Vector3 target = stealth_component.getLastKnownPlayerPos();
    float dist_to_target = Vector2Distance({position.x, position.z}, {target.x, target.z});
    
    if (dist_to_target > 1.0f && investigation_timer <= 0.0f) {
      // Pathfind to last known position
      path_recalc_timer -= current_ctx->dt;
      if (path_recalc_timer <= 0.0f) {
        if (current_ctx->nav_query) {
            current_path = current_ctx->nav_query->findPath(position, target);
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
  auto returnToPostAction = std::make_shared<Action>([this, moveAlongPath]() {
    if (!current_ctx) return NodeState::FAILURE;
    
    float dist_to_post = Vector2Distance({position.x, position.z}, {spawn_position.x, spawn_position.z});
    
    if (dist_to_post > 1.0f) {
      // Pathfind to spawn
      path_recalc_timer -= current_ctx->dt;
      if (path_recalc_timer <= 0.0f) {
        if (current_ctx->nav_query) {
            current_path = current_ctx->nav_query->findPath(position, spawn_position);
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
    float yaw_rad = rotation.y * DEG2RAD;
    Vector3 forward = {std::sin(yaw_rad), 0.0f, std::cos(yaw_rad)};

    Vector3 hitbox_center = {position.x + forward.x * ATTACK_REACH,
                             position.y + (BODY_HEIGHT * 0.5f),
                             position.z + forward.z * ATTACK_REACH};

    Sphere attack_sphere(hitbox_center, ATTACK_RADIUS);

    active_hitboxes.emplace_back(attack_sphere,
                                 15.0f, // Health damage
                                 10.0f, // Posture damage
                                 getFaction(), getId());
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

  return {AssetID::ENEMY_ASHIGARU, transform, animator.renderState()};
}