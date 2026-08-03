#include <Entities/Enemy.h>
#include <raymath.h>
#include <rlgl.h>

Enemy::Enemy(Vector3 start_position, Faction faction) : Character(faction) {
  position = start_position;
  rotation = {0.0f, 0.0f, 0.0f};
}

void Enemy::drawHPBar(const Camera3D &camera) const {

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

  if (stats.isDead()) {
      int font_size = 10;
      int text_width = MeasureText("DEATH", font_size);
      int text_x = static_cast<int>(screen_pos.x - (text_width / 2.0f));
      int text_y = static_cast<int>(screen_pos.y);
      DrawText("DEATH", text_x, text_y, font_size, RED);
      return;
  }

  float width = 60.0f;
  float height = 6.0f;
  int x = static_cast<int>(screen_pos.x - (width / 2.0f));
  int y = static_cast<int>(screen_pos.y);

  DrawRectangle(x, y, (int)width, (int)height, RED);
  DrawRectangle(x, y, (int)(width * stats.getHealthPercentage()), (int)height, LIME);
  DrawRectangleLines(x, y, (int)width, (int)height, BLACK);

  int posture_y = y + (int)height + 2;
  DrawRectangle(x, posture_y, (int)width, (int)height, DARKGRAY);
  DrawRectangle(x, posture_y, (int)(width * stats.getPosturePercentage()), (int)height, ORANGE);
  DrawRectangleLines(x, posture_y, (int)width, (int)height, BLACK);

  int stealth_y = posture_y + (int)height + 2;
  float awareness = stealth_component.getAwarenessLevel();
  float sus_pct = std::min(awareness, 100.0f) / 100.0f;
  float aware_pct = std::max(0.0f, awareness - 100.0f) / 100.0f;

  DrawRectangle(x, stealth_y, (int)width, (int)height, DARKGRAY);
  if (sus_pct > 0.0f) {
      DrawRectangle(x, stealth_y, (int)(width * sus_pct), (int)height, YELLOW);
  }
  if (aware_pct > 0.0f) {
      DrawRectangle(x, stealth_y, (int)(width * aware_pct), (int)height, RED);
  }
  DrawRectangleLines(x, stealth_y, (int)width, (int)height, BLACK);

  // Draw hovering debug text above the HP bar
  const std::string& debug_text = stealth_component.getDebugState();
  int font_size = 10;
  int text_width = MeasureText(debug_text.c_str(), font_size);
  int text_x = static_cast<int>(screen_pos.x - (text_width / 2.0f));
  int text_y = y - font_size - 4; // Above the HP bar
  
  Color text_color;
  StealthState s_state = stealth_component.getStealthState();
  if (s_state == StealthState::Aware) text_color = RED;
  else if (s_state == StealthState::Suspicious) text_color = YELLOW;
  else text_color = GRAY;
  
  DrawText(debug_text.c_str(), text_x, text_y, font_size, text_color);

  // Draw combat state text above stealth text
  std::string combat_text = "";
  switch (combat_component.getCurrentState()) {
    case CombatState::Idle: combat_text = "no-attack"; break;
    case CombatState::AttackStartup: combat_text = "startup"; break;
    case CombatState::AttackActive: combat_text = "active"; break;
    case CombatState::AttackRecovery: combat_text = "recovery"; break;
    case CombatState::Parrying: combat_text = "parry"; break;
    case CombatState::Blocking: combat_text = "block"; break;
    case CombatState::Dodging: combat_text = "dodge"; break;
    case CombatState::PostureBroken: combat_text = "posture-broken"; break;
  }
  
  int combat_width = MeasureText(combat_text.c_str(), font_size);
  int combat_x = static_cast<int>(screen_pos.x - (combat_width / 2.0f));
  int combat_y = text_y - font_size - 2;
  
  Color combat_color = ORANGE;
  if (combat_component.getCurrentState() == CombatState::Idle) combat_color = LIGHTGRAY;
  else if (combat_component.getCurrentState() == CombatState::AttackActive) combat_color = RED;
  
  DrawText(combat_text.c_str(), combat_x, combat_y, font_size, combat_color);
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

void Enemy::takeDamage(float health_damage, float posture_damage, Character* attacker) {
  if (combat_component.getCurrentState() == CombatState::Parrying) {
    health_damage = 0.0f;
    // Posture damage remains normal, same as block
  }
  else if (combat_component.getCurrentState() == CombatState::Blocking) {
    health_damage = 0.0f;
  }

  const bool blocked =
      (combat_component.getCurrentState() == CombatState::Blocking);
  bool was_posture_broken = stats.isPostureBroken();

  const bool hit_applied = stats.applyDamage(health_damage, posture_damage);

  // If posture was already broken (they are in the 3s vulnerable window), next hit executes them
  if (was_posture_broken && stats.isPostureBroken()) {
      stats.applyDamage(stats.getCurrentHealth(), 0.0f);
  } else if (!was_posture_broken && stats.isPostureBroken()) {
      combat_component.breakPosture(3.0f);
  }

  // Once per hit that connected, and only then: a hit swallowed by i-frames is
  // not something the character should be seen reacting to. After the execute
  // above, so a subclass asking whether it is dead gets this hit's answer.
  if (hit_applied)
    onDamaged(blocked);
}