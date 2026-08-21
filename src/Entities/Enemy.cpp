#include <AI/NavMeshQuery.h>
#include <Entities/Enemy.h>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>

Enemy::Enemy(const EnemySpawn &spawn, Faction faction) : Character(faction) {
  position = spawn.at.position;
  // Live rotation and the post it returns to, from one field. Setting these in
  // two places is what let an authored facing survive spawn and then be lost
  // on the first de-aggro.
  rotation = {0.0f, spawn.at.yaw, 0.0f};
  spawn_position = spawn.at.position;
  spawn_yaw = spawn.at.yaw;

  // Applied before any subclass adds its sensors, which is safe: forceAwareness
  // touches only the awareness level and the derived state, and no sensor reads
  // either of them.
  if (spawn.overrides.startAwareness) {
    stealth_component.forceAwareness(*spawn.overrides.startAwareness);
  }
}


// Hoisted out of Swordman, where they were `this`-capturing lambdas inside
// setupBehaviorTree(). Nothing in either is swordman-shaped -- they are pure
// path geometry -- and between them they were the largest block a second enemy
// type would have had to copy. truncatePathBySmoke in particular carries a
// non-obvious unsigned-underflow fix that is not worth anyone re-deriving.

bool Enemy::moveAlongPath(float speed) {
  if (current_path.empty()) {
    this->setHorizontalVelocity({0, 0, 0});
    return true;
  }
  
  Vector3 target = current_path.front();
  Vector3 dir = Vector3Subtract(target, position);
  float dist = Vector2Distance({position.x, position.z}, {target.x, target.z});
  
  if (dist < 0.5f) {
    current_path.erase(current_path.begin());
    if (current_path.empty()) {
      this->setHorizontalVelocity({0, 0, 0});
      return true;
    }
    target = current_path.front();
    dir = Vector3Subtract(target, position);
  }
  
  Vector3 normalized_dir = Vector3Normalize({dir.x, 0.0f, dir.z});
  Vector3 target_vel = {normalized_dir.x * speed, 0.0f, normalized_dir.z * speed};
  this->setHorizontalVelocity(target_vel);
  
  float target_yaw = std::atan2(normalized_dir.x, normalized_dir.z) * RAD2DEG;
  float angle_diff = target_yaw - rotation.y;
  while (angle_diff < -180.0f) angle_diff += 360.0f;
  while (angle_diff > 180.0f) angle_diff -= 360.0f;
  
  float alpha = 10.0f * current_ctx->dt;
  if (alpha > 1.0f) alpha = 1.0f;
  
  rotation.y += angle_diff * alpha;
  while (rotation.y < 0.0f) rotation.y += 360.0f;
  while (rotation.y >= 360.0f) rotation.y -= 360.0f;
  
  return false;
}

void Enemy::truncatePathBySmoke(std::vector<Vector3> &path) {
    if (!current_ctx || !current_ctx->smoke_clouds) return;
    // i + 1 < size(), not i < size() - 1. size() is unsigned, so on an empty
    // path the subtraction wraps to SIZE_MAX, the loop runs, and path[0]
    // dereferences the null data pointer of an empty vector.
    // NavMeshQuery::findPath returns an empty path whenever findNearestPoly
    // fails for either end (NavMeshQuery.cpp:18) — which is what happens the
    // moment the player stands somewhere off the navmesh, such as on top of
    // a castle wall.
    for (size_t i = 0; i + 1 < path.size(); ++i) {
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
}

std::vector<HitBox> Enemy::getActiveHitBoxes() const {
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

void Enemy::updateStrafing(const Vector3& velocity, bool enable_strafing) {
  if (enable_strafing && stealth_component.getStealthState() == StealthState::Aware) {
    is_strafing = true;
    float yaw_rad = rotation.y * DEG2RAD;
    float sin_yaw = std::sin(yaw_rad);
    float cos_yaw = std::cos(yaw_rad);
    
    Vector3 norm_vel = {0.0f, 0.0f, 0.0f};
    float speed_sqr = velocity.x * velocity.x + velocity.z * velocity.z;
    if (speed_sqr > 0.01f) {
      norm_vel = Vector3Normalize(velocity);
    }
    
    // +Z is forward, +X is left in this engine's animation local space
    localMoveDir.z = norm_vel.x * sin_yaw + norm_vel.z * cos_yaw;
    localMoveDir.x = norm_vel.x * cos_yaw - norm_vel.z * sin_yaw;
  } else {
    is_strafing = false;
    localMoveDir = {0.0f, 0.0f, 0.0f};
  }
}

void Enemy::updateCombatCircling(const UpdateContext& ctx, Vector3 target_pos, float move_speed, float rot_speed) {
  in_direct_combat = true;

  if (circle_timer > 0.0f) {
    circle_timer -= ctx.dt;
  }

  Vector3 to_target = Vector3Subtract(target_pos, position);
  to_target.y = 0.0f;
  float distance = Vector3Length(to_target);
  if (distance < 0.001f) {
    this->setHorizontalVelocity({0.0f, 0.0f, 0.0f});
    return;
  }

  Vector3 to_target_norm = Vector3Scale(to_target, 1.0f / distance);

  // 1. Randomize strafe direction when timer expires
  if (circle_timer <= 0.0f) {
    if (rand() % 2 == 0) {
      circle_direction = -1.0f;
    } else {
      circle_direction = 1.0f;
    }
    circle_timer = (rand() % 200 + 200) / 100.0f; // 2.0s to 4.0s
  }

  // 2. Tangent vector for circling around target
  Vector3 tangent = {-to_target_norm.z, 0.0f, to_target_norm.x};
  Vector3 strafe_dir = Vector3Scale(tangent, circle_direction);

  // 3. Radial correction with deadzone [preferred_distance_min, preferred_distance_max]
  float radial_weight = 0.0f;
  if (distance < preferred_distance_min) {
    float underflow = preferred_distance_min - distance;
    radial_weight = -std::fmin(1.0f, underflow * 1.0f);
  } else if (distance > preferred_distance_max) {
    float overflow = distance - preferred_distance_max;
    radial_weight = std::fmin(1.0f, overflow * 1.0f);
  }
  Vector3 radial_dir = Vector3Scale(to_target_norm, radial_weight);

  // 4. Separation from other friendly characters
  Vector3 separation = {0.0f, 0.0f, 0.0f};
  if (ctx.activeCharacters != nullptr) {
    for (const Character* other : *ctx.activeCharacters) {
      if (other == this || other->getFaction() != this->getFaction()) {
        continue;
      }
      Vector3 to_other = Vector3Subtract(other->getPosition(), position);
      to_other.y = 0.0f;
      float dist_other = Vector3Length(to_other);
      if (dist_other < 2.5f && dist_other > 0.001f) {
        float push_weight = 1.0f - (dist_other / 2.5f);
        separation = Vector3Add(separation, Vector3Scale(Vector3Normalize(to_other), -push_weight));

        if (circle_timer <= 0.0f && dist_other < 1.8f && Vector3DotProduct(strafe_dir, to_other) > 0.7f) {
          circle_direction = -circle_direction;
          circle_timer = 2.0f;
        }
      }
    }
  }

  // 5. Combine strafe, radial, and separation directions
  Vector3 desired_dir = Vector3Add(strafe_dir, radial_dir);
  desired_dir = Vector3Add(desired_dir, Vector3Scale(separation, 1.2f));

  Vector3 move_dir = strafe_dir;
  if (Vector3LengthSqr(desired_dir) > 0.001f) {
    move_dir = Vector3Normalize(desired_dir);
  }

  // Speed scaling during distance correction
  float speed_scale = 0.75f + std::abs(radial_weight) * 0.25f;
  float current_speed = move_speed * speed_scale;

  // 6. Ledge / Cliff / NavMesh Edge deflection (prevents falling off cliffs/ramps)
  if (ctx.nav_query != nullptr && Vector3LengthSqr(move_dir) > 0.001f) {
    float probe_dist = 1.2f;
    Vector3 probe_pos = Vector3Add(position, Vector3Scale(move_dir, probe_dist));
    float hit_t = 1.0f;
    Vector3 hit_normal = {0.0f, 0.0f, 0.0f};
    bool is_clear = ctx.nav_query->raycast(position, probe_pos, &hit_t, &hit_normal);
    if (!is_clear) {
      float normal_dot = Vector3DotProduct(move_dir, hit_normal);
      if (normal_dot < 0.0f) {
        // Project onto edge tangent to slide along the ledge
        Vector3 slide_dir = Vector3Subtract(move_dir, Vector3Scale(hit_normal, normal_dot));

        // Push gently inward when very close to edge
        if (hit_t < 0.6f) {
          float push_factor = (0.6f - hit_t) / 0.6f;
          slide_dir = Vector3Add(slide_dir, Vector3Scale(hit_normal, push_factor * 0.8f));
        }

        if (Vector3LengthSqr(slide_dir) > 0.001f) {
          move_dir = Vector3Normalize(slide_dir);
        } else {
          circle_direction = -circle_direction;
          circle_timer = 2.0f;
          move_dir = hit_normal;
        }
      }
    }
  }

  // 7. Exponential velocity smoothing
  Vector3 target_velocity = {move_dir.x * current_speed, 0.0f, move_dir.z * current_speed};
  float lerp_alpha = 1.0f - std::exp(-15.0f * ctx.dt);
  Vector3 old_vel = this->getHorizontalVelocity();
  Vector3 smoothed_vel = Vector3Lerp(old_vel, target_velocity, lerp_alpha);
  this->setHorizontalVelocity(smoothed_vel);

  // 8. Exponential facing rotation smoothing towards target
  float target_yaw = std::atan2(to_target_norm.x, to_target_norm.z) * RAD2DEG;
  float angle_diff = target_yaw - rotation.y;
  while (angle_diff < -180.0f) angle_diff += 360.0f;
  while (angle_diff > 180.0f) angle_diff -= 360.0f;
  float rot_alpha = 1.0f - std::exp(-rot_speed * ctx.dt);
  rotation.y += angle_diff * rot_alpha;
  while (rotation.y < 0.0f) rotation.y += 360.0f;
  while (rotation.y >= 360.0f) rotation.y -= 360.0f;
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

DamageResult Enemy::takeDamage(float health_damage, float posture_damage, Character* attacker) {
  bool can_block = true;
  if (attacker) {
    float yaw_rad = rotation.y * DEG2RAD;
    Vector3 forward = {std::sin(yaw_rad), 0.0f, std::cos(yaw_rad)};
    Vector3 to_attacker = {attacker->getPosition().x - position.x, 0.0f, attacker->getPosition().z - position.z};
    to_attacker = Vector3Normalize(to_attacker);
    if (Vector3DotProduct(forward, to_attacker) < 0.707f) {
      can_block = false;
    }
  }

  if (can_block && combat_component.getCurrentState() == CombatState::Idle) {
    combat_component.startGuard();
    combat_component.stopGuard(); // Auto-release so they don't get stuck blocking
  }

  if (can_block && combat_component.getCurrentState() == CombatState::Parrying) {
    health_damage = 0.0f;
    // Posture damage remains normal, same as block
  }
  else if (can_block && combat_component.getCurrentState() == CombatState::Blocking) {
    health_damage = 0.0f;
  }

  const bool blocked =
      (can_block && combat_component.getCurrentState() == CombatState::Blocking);
  bool was_posture_broken = stats.isPostureBroken();

  const bool hit_applied = stats.applyDamage(health_damage, posture_damage);

  // If posture was already broken (they are in the 3s vulnerable window), next hit executes them
  if (combat_component.getCurrentState() == CombatState::BeingExecuted) {
      stats.applyDamage(stats.getCurrentHealth(), 0.0f);
  } else if (was_posture_broken && stats.isPostureBroken()) {
      stats.applyDamage(stats.getCurrentHealth(), 0.0f);
  } else if (!was_posture_broken && stats.isPostureBroken()) {
      combat_component.breakPosture(3.0f);
  }

  // Once per hit that connected, and only then: a hit swallowed by i-frames is
  // not something the character should be seen reacting to. After the execute
  // above, so a subclass asking whether it is dead gets this hit's answer.
  if (hit_applied) {
      if (attacker) {
          stealth_component.forceAwareness(200.0f);
          stealth_component.setLastKnownPlayerPos(attacker->getPosition());
      }
      const bool parried = (can_block && combat_component.getCurrentState() == CombatState::Parrying);
      onDamaged(blocked, parried);
      if (parried) return DamageResult::PARRIED;
      return blocked ? DamageResult::BLOCKED : DamageResult::HIT;
  }
  return DamageResult::IGNORED;
}