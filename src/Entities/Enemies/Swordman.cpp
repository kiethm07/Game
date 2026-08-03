#include "Entities/Character.h"
#include <Entities/Enemies/Swordman.h>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>
#include <AI/NavGraph.h>

Swordman::Swordman(Vector3 start_position) : Enemy(start_position) {
  stats = Stats(1000.0f, 100.0f, 15.0f);
  combo = {AttackID::PlayerLight1};
  stealth_component.addSensor(std::make_shared<VisionSensor>(20.0f, 70.0f));
  stealth_component.addSensor(std::make_shared<SoundSensor>(6.0f));
  stealth_component.addSensor(std::make_shared<ProximitySensor>(1.2f));
  setupBehaviorTree();
}

void Swordman::update(const UpdateContext &ctx) {
  const float dt = ctx.dt;
  current_ctx = &ctx;

  stats.update(dt);

  if (ctx.assets)
    animator.resolveClips(*ctx.assets);

  // Gameplay stops at death; the animation does not. The death clip has to be
  // driven to its end and held there, which the early return this replaced left
  // frozen on whatever pose the last live frame happened to be showing.
  const bool dead = stats.isDead();
  if (!dead) {
    combat_component.update(dt);
    ai_component.update();
  }

  // A broken guard is a full stop. Read after the tick, not before it: the
  // window may have expired on this very frame, and the tree is entitled to
  // start chasing again the moment it does. The tree's own posture checks only
  // return early — they never touch velocity — so whatever the chase last wrote
  // would otherwise keep sliding the body through the whole deathblow window,
  // which is the one moment the enemy is supposed to be a stationary target.
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

void Swordman::onDamaged(bool blocked) { animator.queueReaction(blocked); }

void Swordman::setupBehaviorTree() {
  using namespace BT;

  auto orientAction = std::make_shared<Action>([this]() {
    if (!current_ctx) return NodeState::FAILURE;
    if (combat_component.getCurrentState() == CombatState::PostureBroken) return NodeState::SUCCESS;
    
    Vector3 dir = Vector3Subtract(current_ctx->playerPos, position);
    if (dir.x != 0.0f || dir.z != 0.0f) {
      float target_yaw = std::atan2(dir.x, dir.z) * RAD2DEG;
      float angle_diff = target_yaw - rotation.y;
      while (angle_diff < -180.0f) angle_diff += 360.0f;
      while (angle_diff > 180.0f) angle_diff -= 360.0f;
      
      float alpha = 10.0f * current_ctx->dt;
      if (alpha > 1.0f) alpha = 1.0f;
      
      rotation.y += angle_diff * alpha;
      while (rotation.y < 0.0f) rotation.y += 360.0f;
      while (rotation.y >= 360.0f) rotation.y -= 360.0f;
    }
    return NodeState::SUCCESS;
  });

  auto stealthCondition = std::make_shared<Condition>([this]() {
    return stealth_component.isPlayerDetected();
  });

  auto attackCondition = std::make_shared<Condition>([this]() {
    if (!current_ctx) return false;
    float distance = Vector3Distance(position, current_ctx->playerPos);
    return (distance < 2.0f && combat_component.getCurrentState() == CombatState::Idle);
  });

  auto attackAction = std::make_shared<Action>([this]() {
    combat_component.initiateCombo(combo);
    return NodeState::SUCCESS;
  });

  auto idleAction = std::make_shared<Action>([this]() {
    // Enemy does nothing when idle
    return NodeState::SUCCESS;
  });

  auto attackSequence = std::make_shared<Sequence>(std::vector<NodePtr>{
    stealthCondition,
    orientAction,
    attackCondition,
    attackAction
  });

  auto chaseAction = std::make_shared<Action>([this]() {
    if (!current_ctx || !current_ctx->nav_graph) return NodeState::FAILURE;
    if (combat_component.getCurrentState() == CombatState::PostureBroken) return NodeState::SUCCESS;

    auto applyLocalAvoidance = [&](Vector3& target_dir) {
      if (!current_ctx->obstacles) return;
      Vector3 avoidance = {0,0,0};
      for (const auto& obs : *current_ctx->obstacles) {
          if (obs.getShape() != ObstacleShape::BOX_SHAPE) continue;
          BoundingBox box = obs.getApproxBox();
          
          // Skip if obstacle is too high or too low
          if (position.y >= box.max.y || position.y + 1.8f <= box.min.y) continue;
          
          float dx = 0.0f, dz = 0.0f;
          if (position.x < box.min.x) dx = box.min.x - position.x;
          else if (position.x > box.max.x) dx = position.x - box.max.x;
          if (position.z < box.min.z) dz = box.min.z - position.z;
          else if (position.z > box.max.z) dz = position.z - box.max.z;
          
          float dist = sqrtf(dx*dx + dz*dz);
          if (dist < 1.5f) { // Whisker radius
              Vector3 center = { (box.min.x + box.max.x)/2.0f, 0.0f, (box.min.z + box.max.z)/2.0f };
              Vector3 push = Vector3Subtract(position, center);
              push.y = 0.0f;
              push = Vector3Normalize(push);
              float strength = (1.5f - dist) / 1.5f;
              avoidance = Vector3Add(avoidance, Vector3Scale(push, strength * 2.0f));
          }
      }
      if (Vector3Length(avoidance) > 0.0f) {
          target_dir = Vector3Normalize(Vector3Add(target_dir, avoidance));
      }
    };

    // DIRECT CHASE: If player is visible and on roughly the same height, just walk straight!
    if (std::abs(current_ctx->playerPos.y - position.y) < 1.5f && stealth_component.isPlayerDetected()) {
      current_path.clear();
      Vector3 dir = Vector3Subtract(current_ctx->playerPos, position);
      Vector3 normalized_dir = Vector3Normalize({dir.x, 0.0f, dir.z});
      applyLocalAvoidance(normalized_dir);
      this->setHorizontalVelocity({normalized_dir.x * MOVEMENT_SPEED, 0.0f, normalized_dir.z * MOVEMENT_SPEED});

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
    }

    path_recalc_timer -= current_ctx->dt;
    if (path_recalc_timer <= 0.0f) {
      current_path = current_ctx->nav_graph->findPath(position, current_ctx->playerPos);
      path_recalc_timer = 1.0f; // Recalculate every 1 second
      
      // PATH SMOOTHING: Don't walk backwards to the closest node if we are already on the way
      if (current_path.size() >= 2) {
        float dist_to_0 = Vector3Distance(position, current_path[0]);
        float dist_to_1 = Vector3Distance(position, current_path[1]);
        float node0_to_1 = Vector3Distance(current_path[0], current_path[1]);
        
        // If we are closer to node 1 than the edge length, or very close to node 0, pop node 0
        if (dist_to_1 < node0_to_1 || dist_to_0 < 1.5f) {
            current_path.erase(current_path.begin());
        }
      } else if (current_path.size() == 1) {
        if (Vector3Distance(position, current_path[0]) < 1.5f) {
            current_path.clear();
        }
      }
    }

    if (!current_path.empty()) {
      Vector3 target = current_path.front();
      Vector3 dir = Vector3Subtract(target, position);
      
      // Ignore vertical difference for distance check to waypoint
      float dist = Vector2Distance({position.x, position.z}, {target.x, target.z});
      
      if (dist < 0.5f) {
        current_path.erase(current_path.begin());
        if (current_path.empty()) {
          this->setHorizontalVelocity({0, 0, 0});
          return NodeState::SUCCESS;
        }
        target = current_path.front();
        dir = Vector3Subtract(target, position);
      }

      Vector3 normalized_dir = Vector3Normalize({dir.x, 0.0f, dir.z});
      applyLocalAvoidance(normalized_dir);
      this->setHorizontalVelocity({normalized_dir.x * MOVEMENT_SPEED, 0.0f, normalized_dir.z * MOVEMENT_SPEED});

      float target_yaw = std::atan2(normalized_dir.x, normalized_dir.z) * RAD2DEG;
      float angle_diff = target_yaw - rotation.y;
      while (angle_diff < -180.0f) angle_diff += 360.0f;
      while (angle_diff > 180.0f) angle_diff -= 360.0f;

      float alpha = 10.0f * current_ctx->dt;
      if (alpha > 1.0f) alpha = 1.0f;

      rotation.y += angle_diff * alpha;
      while (rotation.y < 0.0f) rotation.y += 360.0f;
      while (rotation.y >= 360.0f) rotation.y -= 360.0f;
    } else {
      // If no path found, just fall back to direct line of sight chasing
      Vector3 dir = Vector3Subtract(current_ctx->playerPos, position);
      Vector3 normalized_dir = Vector3Normalize({dir.x, 0.0f, dir.z});
      applyLocalAvoidance(normalized_dir);
      this->setHorizontalVelocity({normalized_dir.x * MOVEMENT_SPEED, 0.0f, normalized_dir.z * MOVEMENT_SPEED});
      
      float target_yaw = std::atan2(normalized_dir.x, normalized_dir.z) * RAD2DEG;
      float angle_diff = target_yaw - rotation.y;
      while (angle_diff < -180.0f) angle_diff += 360.0f;
      while (angle_diff > 180.0f) angle_diff -= 360.0f;

      float alpha = 10.0f * current_ctx->dt;
      if (alpha > 1.0f) alpha = 1.0f;

      rotation.y += angle_diff * alpha;
      while (rotation.y < 0.0f) rotation.y += 360.0f;
      while (rotation.y >= 360.0f) rotation.y -= 360.0f;
    }

    return NodeState::RUNNING;
  });

  auto chaseSequence = std::make_shared<Sequence>(std::vector<NodePtr>{
    stealthCondition,
    chaseAction
  });

  auto susCondition = std::make_shared<Condition>([this]() {
    return stealth_component.getStealthState() == StealthState::Suspicious;
  });

  auto susAction = std::make_shared<Action>([this]() {
    if (!current_ctx) return NodeState::FAILURE;
    if (combat_component.getCurrentState() == CombatState::PostureBroken) return NodeState::SUCCESS;
    
    // FIX: Stop moving when suspicious!
    this->setHorizontalVelocity({0, 0, 0});

    Vector3 dir = Vector3Subtract(stealth_component.getLastKnownPlayerPos(), position);
    if (dir.x != 0.0f || dir.z != 0.0f) {
      float target_yaw = std::atan2(dir.x, dir.z) * RAD2DEG;
      float angle_diff = target_yaw - rotation.y;
      while (angle_diff < -180.0f) angle_diff += 360.0f;
      while (angle_diff > 180.0f) angle_diff -= 360.0f;
      
      float alpha = 10.0f * current_ctx->dt;
      if (alpha > 1.0f) alpha = 1.0f;
      
      rotation.y += angle_diff * alpha;
      while (rotation.y < 0.0f) rotation.y += 360.0f;
      while (rotation.y >= 360.0f) rotation.y -= 360.0f;
    }
    return NodeState::SUCCESS;
  });

  auto susSequence = std::make_shared<Sequence>(std::vector<NodePtr>{
    susCondition,
    susAction
  });

  auto rootSelector = std::make_shared<Selector>(std::vector<NodePtr>{
    attackSequence,
    chaseSequence,
    susSequence,
    idleAction
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