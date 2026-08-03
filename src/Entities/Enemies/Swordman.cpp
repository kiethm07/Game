#include "Entities/Character.h"
#include <Entities/Enemies/Swordman.h>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>
#include <AI/NavMeshQuery.h>

namespace {
/// Walk.glb carries a single clip, whose name is the armature's rather than
/// anything descriptive.
const AnimStateMachine<SwordmanAnimState>::Desc kAnimTable[] = {
    /* Idle */ {"Armature|mixamo.com|Layer0", true, 1.0f, false, 0.0f},
};
} // namespace

Swordman::Swordman(Vector3 start_position)
    : Enemy(start_position), anim(AssetID::ENEMY_ASHIGARU, kAnimTable) {
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

  if (stats.isDead())
    return;

  combat_component.update(dt);

  ai_component.update();

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
    if (!current_ctx || !current_ctx->nav_query) return NodeState::FAILURE;
    if (combat_component.getCurrentState() == CombatState::PostureBroken) return NodeState::SUCCESS;

    path_recalc_timer -= current_ctx->dt;
    if (path_recalc_timer <= 0.0f) {
      current_path = current_ctx->nav_query->findPath(position, current_ctx->playerPos);
      path_recalc_timer = 0.25f; // Recalculate 4 times per second for smooth tracking
    }

    if (!current_path.empty()) {
      Vector3 target = current_path.front();
      Vector3 dir = Vector3Subtract(target, position);
      
      // Ignore vertical difference for waypoint arrival check
      float dist = Vector2Distance({position.x, position.z}, {target.x, target.z});
      
      if (dist < 0.15f) {
        current_path.erase(current_path.begin());
        if (!current_path.empty()) {
          target = current_path.front();
          dir = Vector3Subtract(target, position);
        }
      }

      if (!current_path.empty()) {
        Vector3 normalized_dir = Vector3Normalize({dir.x, 0.0f, dir.z});
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
        this->setHorizontalVelocity({0, 0, 0});
      }
    } else {
      this->setHorizontalVelocity({0, 0, 0});
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

  return {AssetID::ENEMY_ASHIGARU, transform, anim.renderState()};
}