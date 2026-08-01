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

  animator.updateFlinch(dt, ctx.assets);

  SwordmanAnimator::Frame frame;
  frame.combat = &combat_component;
  frame.assets = ctx.assets;
  // Nothing writes an enemy's velocity yet, so this is false every frame today.
  // Read from the same field PhysicsManager integrates, so the stride starts
  // the moment something does.
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

  return {AssetID::ENEMY_ASHIGARU, transform, animator.renderState()};
}