#include "States/GamePlayState.h"
#include "raylib.h"
#include "raymath.h"
#include <Core/Game.h>
#include <cassert>
#include <cmath>

namespace {
/// The level this state loads. One map for now — StateAction carries no
/// payload, so there is nowhere for a level choice to come from until there is
/// a map-select screen to make it.
constexpr const char *kLevelPath = ASSET_DIR "/levels/greybox/level.json";
} // namespace

GameplayState::GameplayState(const InputManager &input_manager)
    : input_manager(input_manager) {
  camera_controller = std::make_unique<CameraController>();

  // The world, authored in Blender and exported by tools/export_level.py.
  // Everything below reads from `level` rather than from literals: geometry,
  // spawns, and the extent the shadow cascades are framed against.
  level = LevelLoader::load(kLevelPath);
  if (!level.valid) {
    // Deliberately not fatal. The player still lands on PhysicsManager's y=0
    // floor clamp, so the game comes up empty and obviously broken with the
    // reason already logged, rather than crashing on startup and taking the
    // reason with it.
    TraceLog(LOG_ERROR,
             "GameplayState: level '%s' failed to load; starting with an "
             "empty world.",
             kLevelPath);
  }
  player = std::make_unique<Player>(input_manager);
  player->setPosition(level.playerSpawn.position);
  player->setRotation({0.0f, level.playerSpawn.yaw, 0.0f});

  for (const EnemySpawn &spawn : level.enemySpawns) {
    auto enemy = EnemyFactory::createEnemy(spawn.type, spawn.at.position);
    enemy->setRotation({0.0f, spawn.at.yaw, 0.0f});
    enemies.push_back(std::move(enemy));
  }

  // --- NAVMESH SETUP ---
  // Obstacles must be complete before build(): the navmesh is baked once, here,
  // and there is no runtime rebuild path. The floor is one of them now — it
  // used to be a 200x200 plate fabricated on the spot for Recast alone, which
  // meant the floor the AI walked on, the floor the renderer drew, and the
  // floor physics clamped to were three separate things that could disagree.
  for (const auto &obs : level.obstacles) {
    nav_builder.addObstacle(obs);
  }
  if (!nav_builder.build()) {
    // Silently discarding this used to be survivable when the geometry was a
    // literal in this file. Now that it comes from a level file, a bake that
    // fails is an authoring problem the author needs told about — otherwise it
    // presents as enemies that simply never move.
    TraceLog(LOG_ERROR,
             "GameplayState: navmesh bake failed for level '%s'. Enemies will "
             "not path. Check that the level has a floor proxy and that gaps "
             "are wider than the 0.6m agent radius.",
             level.name.c_str());
  } else {
    TraceLog(LOG_INFO, "GameplayState: navmesh baked from %d obstacles",
             (int)level.obstacles.size());
  }
  nav_query.init(nav_builder.getNavMesh());

  renderer = std::make_unique<GameRenderer>(asset_manager, level);
}

void GameplayState::enter() {
  // Load local menu-only graphics or titles here
}

StateAction GameplayState::update(float dt) {
  // 1. Tick entities through the shared polymorphic update path. Each reads
  // input/AI internally and shifts its own position safely.
  Vector3 player_pos = player->getPosition();
  const UpdateContext ctx{dt, camera_controller->getCameraForward(),
                          camera_controller->getCameraRight(), player_pos,
                          &asset_manager, &nav_query, &level.obstacles, &smoke_clouds};

  std::vector<Character *> active_characters;
  active_characters.reserve(1 + enemies.size());

  active_characters.push_back(player.get());
  for (auto &enemy : enemies) {
    active_characters.push_back(enemy.get());
  }

  // 1.5. Evaluate Stealth before AI update so AI can react in the same frame
  stealth_manager.update(active_characters, player.get(), level.obstacles, smoke_clouds, dt);

  player->update(ctx);
  for (auto &enemy : enemies) {
    enemy->update(ctx);
  }

  // 2. Resolve Physics Pipeline (4-Step: Gravity -> Integration -> Ejection
  // Loop -> Ground Snap)
  std::vector<Vector3> new_positions =
      physics_manager.updatePhysics(active_characters, level.obstacles, dt);
  for (size_t i = 0; i < active_characters.size(); ++i) {
    active_characters[i]->setPosition(new_positions[i]);
  }

  // 3. Resolve Combat
  combat_manager.update(active_characters, &particle_manager);

  auto checkLineOfSight = [&](Vector3 start, Vector3 end) {
    for (const auto &obs : obstacles) {
      if (obs.getShape() == ObstacleShape::RAMP_SHAPE) {
        int steps = 10;
        for (int i = 1; i <= steps; ++i) {
          float frac = (float)i / steps;
          Vector3 p = Vector3Lerp(start, end, frac);
          if (obs.containsXZ(p, 0.0f)) {
            float h = obs.getHeightAt(p);
            if (p.y < h) return false;
          }
        }
        continue;
      }
      Vector3 local_start = Vector3Transform(start, obs.getWorldToLocal());
      Vector3 local_end = Vector3Transform(end, obs.getWorldToLocal());
      Ray local_ray;
      local_ray.position = local_start;
      local_ray.direction = Vector3Normalize(Vector3Subtract(local_end, local_start));
      RayCollision collision = GetRayCollisionBox(local_ray, obs.getLocalBox());
      if (collision.hit) {
        float dist_to_tgt_local = Vector3Distance(local_start, local_end);
        if (collision.distance < dist_to_tgt_local) return false;
      }
    }
    return true;
  };

  // Helper to get chest height position for raycasting
  auto getChestPos = [](Character* c) {
    Vector3 pos = c->getPosition();
    pos.y += c->getColliderHeight() * 0.8f;
    return pos;
  };

  // Lock-on target validation
  if (locked_target) {
    bool is_visible = checkLineOfSight(getChestPos(player.get()), getChestPos(locked_target));
    if (locked_target->getStats().isDead() || 
        Vector3Distance(player->getPosition(), locked_target->getPosition()) > 20.0f ||
        !is_visible) {
      locked_target = nullptr;
    }
  }

  if (input_manager.isActionPressed(GameAction::LockOn)) {
    if (locked_target) {
      locked_target = nullptr;
    } else {
      float best_score = 10000.0f;
      Character* best_target = nullptr;
      Vector3 cam_pos = camera_controller->getCamera().position;
      Vector3 cam_fwd = camera_controller->getCameraForward();

      for (const auto& enemy : enemies) {
        if (!enemy->getStats().isDead()) {
          float dist_to_player = Vector3Distance(player->getPosition(), enemy->getPosition());
          Vector3 to_enemy = Vector3Subtract(enemy->getPosition(), cam_pos);
          float dist_to_cam = Vector3Length(to_enemy);
          
          if (dist_to_cam < 25.0f) { // Must be within a certain distance from camera
            Vector3 dir = Vector3Scale(to_enemy, 1.0f / dist_to_cam);
            float dot = Vector3DotProduct(cam_fwd, dir);
            
            if (dot > 0.0f) { 
              float angle = acosf(dot) * RAD2DEG;
              if (angle < 45.0f) { // Must be within a 45 degree arc from camera center
                // Score heavily penalizes being off-center, then considers distance
                float score = angle * 2.0f + dist_to_player;
                
                if (score < best_score) {
                  bool is_visible = checkLineOfSight(getChestPos(player.get()), getChestPos(enemy.get()));
                  if (is_visible) {
                    best_score = score;
                    best_target = enemy.get();
                  }
                }
              }
            }
          }
        }
      }
      locked_target = best_target;
    }
  }

  // 4. Update the camera tracking matrix using that position. Built here the
  // same way the UpdateContext above is, so the camera never reaches back into
  // the player for it — and so that the framing, which is a decision about the
  // world rather than about the camera, is made where the world is known.
  // The deathblow shot lasts exactly as long as the swing does — the animation
  // is what the camera is framing, so it is what decides when to let go. The
  // victim is dropped at the same moment, which is what stops a stale pointer
  // from ever reaching the camera.
  if (!player->isExecuting())
    deathblow_victim = nullptr;

  CameraFrame shot;
  shot.target = player->getPosition();
  shot.look = input_manager.getRawMouseDelta();
  shot.dt = dt;
  shot.targetYaw = player->getRotation().y;
  shot.framing =
      player->isDashing() ? CameraFraming::Wide : CameraFraming::Close;
  if (deathblow_victim) {
    shot.shot = CameraShot::Deathblow;
    shot.focus = deathblow_victim->getPosition();
  } else if (locked_target) {
    shot.shot = CameraShot::LockOn;
    shot.focus = locked_target->getPosition();
  }
  shot.obstacles = &obstacles;
  camera_controller->update(shot);

  // Debug affordance, not a game action, so it stays off InputManager's
  // GameAction enum. Shows the raw depth map so the light frustum can be
  // checked against where the arena actually is.
  if (IsKeyPressed(KEY_F1)) {
    show_shadow_map = !show_shadow_map;
  }

  // F2: collision proxies over the level mesh. The check that the Blender
  // export lined up — see GameRenderer::setDebugOverlay.
  if (IsKeyPressed(KEY_F2)) {
    renderer->setDebugOverlay(!renderer->debugOverlayEnabled());
  }

  // F3: cycle what the depth pass renders, for measuring its cost against the
  // fragment-side cost. The stats readout comes up with the F2 overlay.
  if (IsKeyPressed(KEY_F3)) {
    renderer->cycleShadowMode();
  }

  if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
    return StateAction::ChangeToMenu;
  }

  if (smoke_cooldown_timer > 0.0f) {
      smoke_cooldown_timer -= dt;
  }

  // Debug smoke spawning
  if (IsKeyPressed(KEY_O) && smoke_cooldown_timer <= 0.0f) {
      SmokeCloud sc;
      sc.position = player->getPosition();
      sc.radius = 3.5f;
      sc.life = 6.0f;
      sc.owner = player.get();
      smoke_clouds.push_back(sc);
      particle_manager.emitVisualSmoke(sc.position, sc.radius, sc.life);
      smoke_cooldown_timer = 8.0f; // 8 seconds cooldown
  }
  if (IsKeyPressed(KEY_P) && !enemies.empty() && smoke_cooldown_timer <= 0.0f) {
      SmokeCloud sc;
      sc.position = enemies[0]->getPosition();
      sc.radius = 3.5f;
      sc.life = 6.0f;
      sc.owner = enemies[0].get();
      smoke_clouds.push_back(sc);
      particle_manager.emitVisualSmoke(sc.position, sc.radius, sc.life);
      smoke_cooldown_timer = 8.0f; // 8 seconds cooldown
  }

  // Update smoke data lifetimes
  for (int i = (int)smoke_clouds.size() - 1; i >= 0; --i) {
      smoke_clouds[i].life -= dt;
      if (smoke_clouds[i].life <= 0.0f) {
          smoke_clouds[i] = smoke_clouds.back();
          smoke_clouds.pop_back();
      }
  }

  particle_manager.update(dt);

  if (takedown_text_timer > 0.0f) {
    takedown_text_timer -= dt;
  }

  // --- Pending Aerial Takedown Logic ---
  if (pending_aerial_target) {
    if (pending_aerial_target->getStats().isDead()) {
      pending_aerial_target = nullptr; // Abort if target somehow died
    } else {
      Vector3 p_pos = player->getPosition();
      Vector3 e_pos = pending_aerial_target->getPosition();

      // Smoothly lerp X and Z to the target while falling
      float lerp_factor = std::fmin(10.0f * dt, 1.0f);
      float new_x = p_pos.x + (e_pos.x - p_pos.x) * lerp_factor;
      float new_z = p_pos.z + (e_pos.z - p_pos.z) * lerp_factor;
      player->setPosition({new_x, p_pos.y, new_z});

      // If we reach the threshold Y or hit the ground
      if (p_pos.y - e_pos.y < 0.2f || player->isGrounded()) {
        takedown_type_str = "AERIAL TAKEDOWN";
        player->setPosition({e_pos.x, e_pos.y, e_pos.z});
        player->setVerticalVelocity(0.0f);
        player->setRotation(pending_aerial_target->getRotation());

        pending_aerial_target->takeDamage(9999.0f, 0.0f, player.get());
        player->performTakedown();
        deathblow_victim = pending_aerial_target;
        takedown_text_timer = 2.0f;

        pending_aerial_target = nullptr;
      }
    }
  }

  // Takedown logic
  if (!pending_aerial_target &&
      input_manager.isActionPressed(GameAction::Takedown)) {
    for (auto &enemy_ptr : enemies) {
      Enemy *enemy = enemy_ptr.get();
      if (!enemy->getStats().isDead()) {
        Vector3 p_pos = player->getPosition();
        Vector3 e_pos = enemy->getPosition();

        Vector2 p2 = {p_pos.x, p_pos.z};
        Vector2 e2 = {e_pos.x, e_pos.z};
        float horiz_dist = Vector2Distance(p2, e2);
        float vert_dist = p_pos.y - e_pos.y;

        // Tighter vertical check for grounded takedowns so you don't do
        // grounded takedowns from a ledge
        bool is_normal_range =
            (horiz_dist < 2.0f && std::abs(vert_dist) < 0.5f);
        bool is_aerial_range = (horiz_dist < 2.5f && vert_dist >= 1.5f &&
                                vert_dist < 10.0f && !player->isGrounded());

        if (is_normal_range || is_aerial_range) {
          // Raycast check to prevent takedowns through walls
          bool is_blocked = false;
          Vector3 p_head = {
              p_pos.x, p_pos.y + player->getColliderHeight() * 0.8f, p_pos.z};
          Vector3 e_head = {
              e_pos.x, e_pos.y + enemy->getColliderHeight() * 0.8f, e_pos.z};

          for (const auto &obs : level.obstacles) {
            Vector3 local_obs = Vector3Transform(p_head, obs.getWorldToLocal());
            Vector3 local_tgt = Vector3Transform(e_head, obs.getWorldToLocal());

            Ray local_ray;
            local_ray.position = local_obs;
            local_ray.direction =
                Vector3Normalize(Vector3Subtract(local_tgt, local_obs));

            RayCollision collision =
                GetRayCollisionBox(local_ray, obs.getLocalBox());
            if (collision.hit) {
              float dist_to_tgt_local = Vector3Distance(local_obs, local_tgt);
              if (collision.distance < dist_to_tgt_local) {
                is_blocked = true;
                break;
              }
            }
          }

          if (is_blocked)
            continue;

          bool can_takedown = false;
          bool is_aerial = is_aerial_range && !is_normal_range;
          StealthState s_state = enemy->getStealthComponent().getStealthState();

          // 1. Aerial Takedown
          if (is_aerial && (s_state == StealthState::Unaware ||
                            s_state == StealthState::Suspicious)) {
            can_takedown = true;
          }
          // 2. Stealth Takedown (Must be Unaware or Suspicious, and player
          // closely behind)
          else if (s_state == StealthState::Unaware ||
                   s_state == StealthState::Suspicious) {
            Vector3 enemy_fwd = {std::sin(enemy->getRotation().y * DEG2RAD),
                                 0.0f,
                                 std::cos(enemy->getRotation().y * DEG2RAD)};
            Vector3 to_player = Vector3Normalize(Vector3Subtract(p_pos, e_pos));
            float dot = Vector3DotProduct(enemy_fwd, to_player);
            if (dot <
                -0.8f) { // Narrower cone (approx 74 degrees directly behind)
              can_takedown = true;
            }
          }
          // 3. Combat Deathblow (Enemy posture broken)
          else if (enemy->getStats().isPostureBroken()) {
            can_takedown = true;
          }

          if (can_takedown) {
            if (is_aerial) {
              // Trigger the drop phase!
              pending_aerial_target = enemy;
              // Do NOT snap X and Z instantly here; it will lerp smoothly in
              // the update loop Let normal gravity handle the fall instead of
              // boosting it
            } else {
              // Snap rotation and position for grounded takedowns
              if (enemy->getStats().isPostureBroken() &&
                  s_state != StealthState::Unaware &&
                  s_state != StealthState::Suspicious) {
                takedown_type_str = "COMBAT DEATHBLOW";
                // Combat takedown: face the enemy
                Vector3 to_enemy =
                    Vector3Normalize(Vector3Subtract(e_pos, p_pos));
                float target_yaw = std::atan2(to_enemy.x, to_enemy.z) * RAD2DEG;
                player->setRotation({0.0f, target_yaw, 0.0f});

                // Snap position 1.2 units in front of enemy
                player->setPosition({e_pos.x - to_enemy.x * 1.2f, p_pos.y,
                                     e_pos.z - to_enemy.z * 1.2f});
              } else {
                takedown_type_str = "STEALTH TAKEDOWN";
                // Stealth backstab: align exactly with enemy's facing direction
                player->setRotation(enemy->getRotation());

                // Snap position 1.2 units exactly behind the enemy
                float enemy_yaw = enemy->getRotation().y * DEG2RAD;
                Vector3 backward = {-std::sin(enemy_yaw), 0.0f,
                                    -std::cos(enemy_yaw)};
                player->setPosition({e_pos.x + backward.x * 1.2f, p_pos.y,
                                     e_pos.z + backward.z * 1.2f});
              }

              enemy->takeDamage(9999.0f, 0.0f, player.get());
              player->performTakedown();
              deathblow_victim = enemy;
              takedown_text_timer = 2.0f;
            }
            break; // Only execute one enemy
          }
        }
      }
    }
  }

  return StateAction::KeepCurrent;
}

void GameplayState::draw() {
  renderList.clear();
  renderList.push_back(player->getRenderData());
  for (const auto &enemy : enemies) {
    renderList.push_back(enemy->getRenderData());
  }

  // PASS 1 — shadow depth. Renders into its own framebuffers, so it has to
  // happen before the scene is cleared, and it needs the same render list the
  // scene pass gets or the shadows would be a frame stale. The player position
  // is what the near cascade recentres on.
  renderer->renderShadowPass(level.obstacles, renderList, player->getPosition());

  // PASS 2 — the scene.
  ClearBackground(RAYWHITE);
  BeginMode3D(camera_controller->getCamera());

  // Level mesh, obstacles and entities, drawn into the 3D scope opened above.
  renderer->renderGameplay(*camera_controller, level.obstacles, renderList);

  particle_manager.draw();

  std::vector<Character *> active_characters;
  active_characters.reserve(1 + enemies.size());
  active_characters.push_back(player.get());
  for (const auto &enemy : enemies) {
    active_characters.push_back(enemy.get());
  }

  // Collision debug wireframes — must stay inside the 3D scope.
  physics_manager.drawDebug(active_characters, level.obstacles);
  combat_manager.drawDebug(active_characters);
  stealth_manager.drawDebug(active_characters);

  if (locked_target) {
    Vector3 chest_pos = locked_target->getPosition();
    chest_pos.y += 1.3f; // Approximate chest height
    
    // Pull the sphere towards the camera so it doesn't get submerged in the model
    Vector3 cam_pos = camera_controller->getCamera().position;
    Vector3 to_cam = Vector3Normalize(Vector3Subtract(cam_pos, chest_pos));
    chest_pos = Vector3Add(chest_pos, Vector3Scale(to_cam, locked_target->getColliderRadius() + 0.1f));
    
    DrawSphere(chest_pos, 0.04f, WHITE);
  }

  EndMode3D();

  // 2D overlay pass (after the 3D scope is closed).
  renderer->drawUI();
  if (show_shadow_map) {
    renderer->drawShadowMapDebug();
  }

  // --- HEALTH BARS ---
  player->drawHPBar2D();
  for (const auto &enemy : enemies) {
    enemy->drawHPBar(camera_controller->getCamera());
  }

  if (takedown_text_timer > 0.0f) {
    const char *text = takedown_type_str.c_str();
    int font_size = 40;
    int text_width = MeasureText(text, font_size);
    DrawText(text, GetScreenWidth() / 2 - text_width / 2,
             GetScreenHeight() / 2 - 100, font_size, RED);
  }

  // --- SMOKE SCREEN EFFECT ---
  if (player->isInSmoke()) {
      // Draw a full-screen semi-transparent gray overlay
      DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), {100, 100, 100, 150});
  }
}

void GameplayState::exit() {
  // Clean up local menu resources here
}