#include "Entities/Character.h"
#include <Entities/Enemies/Swordman.h>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>

Swordman::Swordman(Vector3 start_position) : Enemy(start_position) {
  combo = {AttackID::PlayerLight1};
}

void Swordman::update(const UpdateContext &ctx) {
  const float dt = ctx.dt;
  const Vector3 &player_position = ctx.playerPos;
  stats.update(dt);

  if (stats.isDead())
    return;

  combat_component.update(dt);

  updateAI(dt, player_position);
}

void Swordman::updateAI(float dt, const Vector3 &player_position) {
  // Orient toward player
  Vector3 dir = Vector3Subtract(player_position, position);
  if (dir.x != 0.0f || dir.z != 0.0f) {
    float target_yaw = std::atan2(dir.x, dir.z) * RAD2DEG;
    rotation.y = target_yaw;
  }

  // Trigger attack when within range
  float distance = Vector3Distance(position, player_position);
  if (distance < 2.0f &&
      combat_component.getCurrentState() == CombatState::Idle) {
    combat_component.initiateCombo(combo);
  }
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
  anim_state.animIndex = currentAnimIndex;
  anim_state.animTime = animTime;

  return {AssetID::ENEMY_ASHIGARU, transform, anim_state};
}