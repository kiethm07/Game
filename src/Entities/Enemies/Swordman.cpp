#include "Entities/Character.h"
#include <Entities/Enemies/Swordman.h>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>

Swordman::Swordman(Vector3 start_position) : Enemy(start_position) {
  stats = Stats(1000.0f, 100.0f, 15.0f);
  combo = {AttackID::PlayerLight1};
  stealth_component.addSensor(std::make_shared<RadiusSensor>(5.0f));
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

  // Walk.glb carries a single clip; resolve it by name rather than assuming an
  // index. Advancing playback here is what the previous code omitted, which
  // left enemies rendering frozen on frame 0.
  if (ctx.assets && !clips.resolved) {
    clips.resolved = true;
    clips.idle = ctx.assets->findAnimation(AssetID::ENEMY_ASHIGARU,
                                           "Armature|mixamo.com|Layer0");
    if (clips.idle < 0) clips.idle = 0;
  }

  animation.play(clips.idle, true);
  const RootMotion::Track &track =
      ctx.assets ? ctx.assets->getRootMotion(AssetID::ENEMY_ASHIGARU,
                                             animation.index())
                 : RootMotion::Track{};
  animation.advance(dt, track.duration);
}

void Swordman::setupBehaviorTree() {
  using namespace BT;

  auto orientAction = std::make_shared<Action>([this]() {
    if (!current_ctx) return NodeState::FAILURE;
    Vector3 dir = Vector3Subtract(current_ctx->playerPos, position);
    if (dir.x != 0.0f || dir.z != 0.0f) {
      float target_yaw = std::atan2(dir.x, dir.z) * RAD2DEG;
      rotation.y = target_yaw;
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

  auto combatSequence = std::make_shared<Sequence>(std::vector<NodePtr>{
    stealthCondition,
    orientAction,
    attackCondition,
    attackAction
  });

  auto rootSelector = std::make_shared<Selector>(std::vector<NodePtr>{
    combatSequence,
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

  AnimationState anim_state;
  anim_state.animIndex = animation.index();
  anim_state.animTime = animation.time();

  return {AssetID::ENEMY_ASHIGARU, transform, anim_state};
}