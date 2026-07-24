#include <Entities/Enemy.h>
#include <raymath.h>
#include <rlgl.h>

Enemy::Enemy(Vector3 start_position, Faction faction) : Character(faction) {
  position = start_position;
  rotation = {0.0f, 0.0f, 0.0f};
}

void Enemy::drawHPBar(const Camera3D &camera) const {
  if (stats.isDead())
    return;

  Vector3 head_pos = {position.x, position.y + body_height + 0.2f, position.z};

  Vector3 cam_forward =
      Vector3Normalize(Vector3Subtract(camera.target, camera.position));
  Vector3 to_enemy = Vector3Subtract(head_pos, camera.position);

  if (Vector3DotProduct(cam_forward, to_enemy) <= 0.0f)
    return;

  Vector2 screen_pos = GetWorldToScreen(head_pos, camera);

  if (screen_pos.x < 0 || screen_pos.x > GetScreenWidth() || screen_pos.y < 0 ||
      screen_pos.y > GetScreenHeight()) {
    return;
  }

  float width = 60.0f;
  float height = 6.0f;
  int x = static_cast<int>(screen_pos.x - (width / 2.0f));
  int y = static_cast<int>(screen_pos.y);

  DrawRectangle(x, y, (int)width, (int)height, RED);
  DrawRectangle(x, y, (int)(width * stats.getHealthPercentage()), (int)height,
                LIME);
  DrawRectangleLines(x, y, (int)width, (int)height, BLACK);
}

float Enemy::getColliderRadius() const { return body_radius; }

float Enemy::getColliderHeight() const { return body_height; }

std::vector<HurtBox> Enemy::getHurtBoxes() const {
  if (stats.isDead())
    return {};

  Capsule body_capsule =
      Capsule::createUpright(position, body_height, body_radius);
  return {HurtBox(body_capsule, getFaction(), getId())};
}

void Enemy::takeDamage(float health_damage, float posture_damage) {
  if (combat_component.getCurrentState() == CombatState::Parrying)
    return;
  if (combat_component.getCurrentState() == CombatState::Blocking)
    health_damage = 0.0f;

  stats.applyDamage(health_damage, posture_damage);
}