#include "States/GamePlayState.h"
#include "raylib.h"
#include "raymath.h"
#include <Core/Game.h>
#include <cassert>
#include <cmath>

GameplayState::GameplayState(const InputManager &input_manager, AssetManager &asset_manager, SoundController &sound_controller)
    : input_manager(input_manager), asset_manager(asset_manager), sound_controller(sound_controller) {
  camera_controller = std::make_unique<CameraController>();
  player = std::make_unique<Player>(input_manager);
  player->setPosition({0.0f, 0.0f, -13.0f}); // Spawn north of the central hub

  // Load test SFX
  asset_manager.loadSound(AssetID::SFX_COIN, ASSET_DIR "/audio/coin.wav");
  asset_manager.loadSound(AssetID::SFX_HIT, ASSET_DIR "/audio/hit.mp3");
  asset_manager.loadSound(AssetID::SFX_DEFLECT_1, ASSET_DIR "/audio/deflect_1.MP3");
  asset_manager.loadSound(AssetID::SFX_DEFLECT_2, ASSET_DIR "/audio/Deflect_2.MP3");
  asset_manager.loadSound(AssetID::SFX_DEFLECT_NPC, ASSET_DIR "/audio/deflect_NPC.MP3");
  asset_manager.loadSound(AssetID::SFX_DEATHBLOW, ASSET_DIR "/audio/deflect_end.mp3");

  // Enemies
  enemies.push_back(
      EnemyFactory::createEnemy(EnemyType::Swordman, {8.0f, 0.0f, 8.0f})); // South-East
  enemies.push_back(
      EnemyFactory::createEnemy(EnemyType::Swordman, {-8.0f, 0.0f, 8.0f})); // South-West
  
  // Test enemy for wall-takedown
  enemies.push_back(
      EnemyFactory::createEnemy(EnemyType::Swordman, {8.0f, 0.0f, -8.0f})); // North-East

  // Test enemies standing next to each other
  auto e1 = EnemyFactory::createEnemy(EnemyType::Swordman, {4.0f, 0.0f, -13.0f});
  e1->setRotation({0.0f, 0.0f, 0.0f});
  enemies.push_back(std::move(e1));

  auto e2 = EnemyFactory::createEnemy(EnemyType::Swordman, {5.5f, 0.0f, -13.0f});
  e2->setRotation({0.0f, 0.0f, 0.0f});
  enemies.push_back(std::move(e2));

  // --- OPEN SCATTERED ARENA FOR PATHFINDING TEST ---
  
  // 1. Central Hub Platform
  obstacles.emplace_back(Vector3{-4.0f, 0.0f, -4.0f}, Vector3{4.0f, 2.5f, 4.0f}, 0.0f, DARKBLUE);

  // 2. Ramps leading up to the Central Hub from 4 directions
  // North Ramp (Z=-11 to -4)
  obstacles.emplace_back(Vector2{-2.0f, -11.0f}, Vector2{2.0f, -4.0f}, 0.0f, 2.5f, 0.0f, SKYBLUE);
  // South Ramp (Z=4 to 11)
  obstacles.emplace_back(Vector2{-2.0f, 4.0f}, Vector2{2.0f, 11.0f}, 2.5f, 0.0f, 0.0f, SKYBLUE);
  // West Ramp (X=-11 to -4)
  obstacles.emplace_back(Vector2{-11.0f, -2.0f}, Vector2{-4.0f, 2.0f}, 0.0f, 2.5f, 0.0f, SKYBLUE);
  // East Ramp (X=4 to 11)
  obstacles.emplace_back(Vector2{4.0f, -2.0f}, Vector2{11.0f, 2.0f}, 2.5f, 0.0f, 0.0f, SKYBLUE);

  // 3. Scattered Pillars / Walls
  // A wall blocking the South Ramp's exit slightly
  obstacles.emplace_back(Vector3{-4.0f, 0.0f, 13.0f}, Vector3{4.0f, 4.0f, 14.0f}, 0.0f, GRAY);
  
  // A large pillar near the West Ramp
  obstacles.emplace_back(Vector3{-13.0f, 0.0f, -5.0f}, Vector3{-10.0f, 6.0f, -2.0f}, 0.0f, DARKGRAY);

  // A standalone smaller platform with its own ramp in the corner
  obstacles.emplace_back(Vector3{10.0f, 0.0f, -15.0f}, Vector3{15.0f, 2.0f, -10.0f}, 0.0f, ORANGE);
  obstacles.emplace_back(Vector2{10.0f, -10.0f}, Vector2{15.0f, -5.0f}, 2.0f, 0.0f, 0.0f, LIME);

  // --- NAVMESH SETUP ---
  for (const auto& obs : obstacles) {
      nav_builder.addObstacle(obs);
  }
  // Add a massive ground plane so the enemies can walk on the floor
  PhysicsObstacle ground({-100.0f, -1.0f, -100.0f}, {100.0f, 0.0f, 100.0f});
  nav_builder.addObstacle(ground);
  nav_builder.build();
  nav_query.init(nav_builder.getNavMesh());

  renderer = std::make_unique<GameRenderer>(asset_manager);
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
                          &asset_manager, &nav_query, &obstacles, &smoke_clouds, locked_target};

  std::vector<Character *> active_characters;
  active_characters.reserve(1 + enemies.size());

  active_characters.push_back(player.get());
  for (auto &enemy : enemies) {
    if (enemy->isModelUnloaded()) continue;
    active_characters.push_back(enemy.get());
  }

  // 1.5. Evaluate Stealth before AI update so AI can react in the same frame
  stealth_manager.update(active_characters, player.get(), obstacles, smoke_clouds, dt);

  player->update(ctx);
  for (auto &enemy : enemies) {
    if (enemy->isModelUnloaded()) continue;

    enemy->update(ctx);
    
    // Check for loot drops and handle decaying
    if (enemy->getStats().isDead()) {
        if (!enemy->hasDroppedLoot()) {
            enemy->setDroppedLoot(true);
            MoneyDrop md;
            md.position = enemy->getPosition();
            md.amount = 10 + rand() % 15;
            md.bob_timer = 0.0f;
            money_drops.push_back(md);
        }
        
        if (enemy->isKilledByStealth()) {
            enemy->addDissolveTimer(dt);
        }
    }
  }


  // Check for money pickups
  for (int i = (int)money_drops.size() - 1; i >= 0; --i) {
      money_drops[i].bob_timer += dt;
      if (Vector3DistanceSqr(player_pos, money_drops[i].position) < 2.0f * 2.0f) {
          player->addMoney(money_drops[i].amount);
          sound_controller.playSFX(AssetID::SFX_COIN);
          money_drops[i] = money_drops.back();
          money_drops.pop_back();
      }
  }

  // 2. Resolve Physics Pipeline (4-Step: Gravity -> Integration -> Ejection
  // Loop -> Ground Snap)
  std::vector<Vector3> new_positions =
      physics_manager.updatePhysics(active_characters, obstacles, dt);
  for (size_t i = 0; i < active_characters.size(); ++i) {
    active_characters[i]->setPosition(new_positions[i]);
  }

  // 3. Resolve Combat
  combat_manager.update(active_characters, &particle_manager, &sound_controller);

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
        if (enemy->isModelUnloaded()) continue;

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

  if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
    return StateAction::ChangeToMenu;
  }

  if (smoke_cooldown_timer > 0.0f) {
      smoke_cooldown_timer -= dt;
  }

  // Process smoke clouds spawned by items (e.g. Smoke Bomb)
  std::vector<SmokeCloud> new_clouds = player->takePendingSmokeClouds();
  for (const auto& sc : new_clouds) {
      smoke_clouds.push_back(sc);
      particle_manager.emitVisualSmoke(sc.position, sc.radius, sc.life);
  }

  // Debug smoke spawning on enemy
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

      // Face the enemy while falling
      Vector3 to_enemy = Vector3Subtract(e_pos, p_pos);
      if (to_enemy.x * to_enemy.x + to_enemy.z * to_enemy.z > 0.001f) {
          float target_yaw = std::atan2(to_enemy.x, to_enemy.z) * RAD2DEG;
          player->setRotation({0.0f, target_yaw, 0.0f});
      }

      // If we reach the threshold Y or hit the ground
      if (p_pos.y - e_pos.y < 0.2f || player->isGrounded()) {
        takedown_type_str = "AERIAL TAKEDOWN";
        
        // Snap position 1.2 units away so they aren't inside each other (prevents physics ejection)
        Vector3 dir = to_enemy;
        dir.y = 0.0f;
        if (Vector3LengthSqr(dir) > 0.001f) {
            dir = Vector3Normalize(dir);
            player->setPosition({e_pos.x - dir.x * 1.2f, e_pos.y, e_pos.z - dir.z * 1.2f});
        } else {
            player->setPosition({e_pos.x, e_pos.y, e_pos.z + 1.2f});
        }
        
        player->setVerticalVelocity(0.0f);
        // Player keeps the rotation looking at the enemy
        
        // Force the enemy to face away from the player (turn their back to the player)
        // so the execution animation doesn't look like they are face-to-face
        pending_aerial_target->setRotation(player->getRotation());

        // Let the hitbox apply the damage and blood in sync with the animation
        if (Enemy* e = dynamic_cast<Enemy*>(pending_aerial_target)) {
            e->getCombatComponent().setBeingExecuted();
            e->setKilledByStealth(true);
            e->setDecayType(DecayType::PETAL_DECAY);
        }
        sound_controller.playSFX(AssetID::SFX_DEATHBLOW);
        player->performTakedown();
        deathblow_victim = pending_aerial_target;
        takedown_text_timer = 2.0f;

        pending_aerial_target = nullptr;
      }
    }
  }

  // Takedown logic
  if (!pending_aerial_target && !player->isExecuting() &&
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

          for (const auto &obs : obstacles) {
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

          bool in_smoke = false;
          for (const auto& sc : smoke_clouds) {
              if (sc.owner != enemy && Vector3DistanceSqr(e_pos, sc.position) <= sc.radius * sc.radius) {
                  in_smoke = true;
                  break;
              }
          }

          // 1. Aerial Takedown
          if (is_aerial && (s_state == StealthState::Unaware ||
                            s_state == StealthState::Suspicious || in_smoke)) {
            can_takedown = true;
          }
          // 2. Stealth Takedown (Must be Unaware or Suspicious, or blinded by smoke, and player
          // closely behind)
          else if (s_state == StealthState::Unaware ||
                   s_state == StealthState::Suspicious || in_smoke) {
            
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
                enemy->setKilledByStealth(true);
                enemy->setDecayType(DecayType::ASH_DECAY);
              }

              // Let the hitbox apply the damage and blood in sync with the animation
              enemy->getCombatComponent().setBeingExecuted();
              sound_controller.playSFX(AssetID::SFX_DEATHBLOW);
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

  // Cleanup fully dissolved bodies at the very end of the frame
  for (auto& enemy : enemies) {
      if (enemy->isFullyDissolved() && !enemy->isModelUnloaded()) {
          if (locked_target == enemy.get()) locked_target = nullptr;
          if (deathblow_victim == enemy.get()) deathblow_victim = nullptr;
          if (pending_aerial_target == enemy.get()) pending_aerial_target = nullptr;
          enemy->setModelUnloaded(true);
      }
  }

  return StateAction::KeepCurrent;
}

void GameplayState::draw() {
  ClearBackground(RAYWHITE);
  BeginMode3D(camera_controller->getCamera());

  // Draw all obstacles (walls, ramps, platforms)
  for (const PhysicsObstacle &obs : obstacles) {
    obs.draw();
  }

  renderList.clear();
  renderList.push_back(player->getRenderData());
  for (const auto &enemy : enemies) {
    if (enemy->isModelUnloaded()) continue;
    renderList.push_back(enemy->getRenderData());
  }

  // World + entities, drawn into the 3D scope opened above.
  renderer->renderGameplay(*camera_controller, renderList);
  
  particle_manager.draw();

  // Draw Money Drops
  for (const auto& md : money_drops) {
      float y_offset = sinf(md.bob_timer * 3.0f) * 0.2f + 0.5f;
      Vector3 draw_pos = {md.position.x, md.position.y + y_offset, md.position.z};
      DrawSphere(draw_pos, 0.15f, GOLD);
  }

  std::vector<Character *> active_characters;
  active_characters.reserve(1 + enemies.size());
  active_characters.push_back(player.get());
  for (const auto &enemy : enemies) {
    if (enemy->isModelUnloaded()) continue;
    active_characters.push_back(enemy.get());
  }

  // Collision debug wireframes — must stay inside the 3D scope.
  physics_manager.drawDebug(active_characters, obstacles);
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

  // --- HEALTH BARS ---
  player->drawHPBar2D();
  for (const auto &enemy : enemies) {
    if (enemy->isModelUnloaded()) continue;
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

  // --- MONEY UI ---
  if (IsKeyDown(KEY_I)) {
      std::string money_str = "Money: " + std::to_string(player->getMoney());
      int font_size = 30;
      int text_width = MeasureText(money_str.c_str(), font_size);
      DrawText(money_str.c_str(), GetScreenWidth() - text_width - 30, 30, font_size, GOLD);
  }

  // --- ITEM UI ---
  const auto& inventory = player->getInventory();
  if (!inventory.empty()) {
      int active_idx = player->getActiveItemIndex();
      if (active_idx >= 0 && active_idx < inventory.size()) {
          const auto& active_item = inventory[active_idx];
          std::string item_text = "[Q] < " + active_item->getName() + " (" + std::to_string(active_item->getCount()) + ") > [E]   Use: [X]";
          int item_font_size = 20;
          DrawText(item_text.c_str(), 20, GetScreenHeight() - 70, item_font_size, active_item->isEmpty() ? GRAY : BLACK);

          // If using, draw a progress bar
          float use_timer = player->getItemUseTimer();
          if (use_timer > 0.0f) {
              float duration = active_item->getUseDuration();
              float progress = 1.0f - (use_timer / duration);
              DrawRectangle(20, GetScreenHeight() - 85, 200, 10, DARKGRAY);
              DrawRectangle(20, GetScreenHeight() - 85, (int)(200 * progress), 10, SKYBLUE);
          }
      }
  }
}

void GameplayState::exit() {
  // Clean up local menu resources here
}