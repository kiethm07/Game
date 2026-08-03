#include "States/GamePlayState.h"
#include "raylib.h"
#include "raymath.h"
#include <Core/Game.h>
#include <cassert>
#include <cmath>

GameplayState::GameplayState(const InputManager &input_manager)
    : input_manager(input_manager) {
  camera_controller = std::make_unique<CameraController>();
  player = std::make_unique<Player>(input_manager);

  // Spawn test enemies via factory
  enemies.push_back(
      EnemyFactory::createEnemy(EnemyType::Swordman, {0.0f, 0.0f, 5.0f}));
  enemies.push_back(
      EnemyFactory::createEnemy(EnemyType::Swordman, {3.0f, 0.0f, 8.0f}));
  
  // Test enemy for wall-takedown
  enemies.push_back(
      EnemyFactory::createEnemy(EnemyType::Swordman, {0.0f, 0.0f, -3.0f}));

  // --- MULTI-LEVEL BUILDING EXAMPLE ---

  // Floor 1 (Ceiling for Ground)
  // Walk under this (Z=5 to 15) to test flat overhead ceilings!
  obstacles.emplace_back(Vector3{-6.0f, 3.0f, 5.0f}, Vector3{6.0f, 3.5f, 15.0f},
                         0.0f, DARKBLUE);

  // Staircase 1: Ground (Y=0) to Floor 1 (Y=3.5)
  // Placed on the left side. Walk under this to test slanted ramp ceilings!
  obstacles.emplace_back(Vector2{-9.0f, -2.0f}, Vector2{-6.0f, 8.0f}, 0.0f,
                         3.5f, 0.0f, SKYBLUE);

  // Floor 2 (Ceiling for Floor 1)
  obstacles.emplace_back(Vector3{-6.0f, 7.0f, 5.0f}, Vector3{6.0f, 7.5f, 15.0f},
                         0.0f, DARKGREEN);

  // Staircase 2: Floor 1 (Y=3.5) to Floor 2 (Y=7.5)
  // Placed on the right side.
  obstacles.emplace_back(Vector2{6.0f, 5.0f}, Vector2{9.0f, 15.0f}, 3.5f, 7.5f,
                         0.0f, LIME);

  // Rotated Pillar supporting the center
  // obstacles.emplace_back(Vector3{-2.0f, 0.0f, 8.0f},
  // Vector3{2.0f, 7.0f, 12.0f},
  //                        45.0f, RED);

  // 4 x 4 grid of pillars
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      obstacles.emplace_back(Vector3{i * 2.0f, 0.0f, j * 2.0f},
                             Vector3{(i + 1) * 2.0f, 10.0f, (j + 1) * 2.0f},
                             0.0f, BLUE);
    }
  }

  // --- THIN WALL FOR TUNNELING TEST ---
  // A wall that is only 0.05 units thick!
  obstacles.emplace_back(Vector3{-5.0f, 0.0f, -5.0f},
                         Vector3{-4.95f, 4.5f, 5.0f}, 0.0f, MAGENTA);

  // --- WALL FOR TAKEDOWN TEST ---
  // Blocks the enemy at Z=-3.0f
  obstacles.emplace_back(Vector3{-2.0f, 0.0f, -2.1f},
                         Vector3{2.0f, 4.5f, -2.0f}, 0.0f, ORANGE);

  // --- TAKEDOWN DROP TARGET ---
  obstacles.emplace_back(Vector3{0.0f, 0.0f, 15.0f}, Vector3{2.0f, 1.0f, 17.0f},
                         0.0f, YELLOW);

  // --- NAVGRAPH SETUP ---
  // Ground network
  int n_ground_stair = nav_graph.addNode({-7.5f, 0.0f, -4.0f}); // Base of stairs 1
  int n_ground_center = nav_graph.addNode({-3.0f, 0.0f, 5.0f}); // Center open area
  int n_ground_right = nav_graph.addNode({5.0f, 0.0f, -3.0f});  // Right side of map
  int n_ground_back = nav_graph.addNode({5.0f, 0.0f, 15.0f});   // Back right of map

  // Link ground nodes to form a path around pillars
  nav_graph.addUndirectedEdge(n_ground_stair, n_ground_center);
  nav_graph.addUndirectedEdge(n_ground_center, n_ground_right);
  nav_graph.addUndirectedEdge(n_ground_right, n_ground_back);
  nav_graph.addUndirectedEdge(n_ground_center, n_ground_back);

  // Stairs & Floors
  int n_stair1_mid = nav_graph.addNode({-7.5f, 1.75f, 3.0f}); // Mid stairs 1
  int n_floor1 = nav_graph.addNode({0.0f, 3.5f, 10.0f});      // Floor 1
  int n_stair2_mid = nav_graph.addNode({7.5f, 5.5f, 10.0f});  // Mid stairs 2
  int n_floor2 = nav_graph.addNode({0.0f, 7.5f, 10.0f});      // Floor 2
  
  // Link verticality
  nav_graph.addUndirectedEdge(n_ground_stair, n_stair1_mid);
  nav_graph.addUndirectedEdge(n_stair1_mid, n_floor1);
  nav_graph.addUndirectedEdge(n_floor1, n_stair2_mid);
  nav_graph.addUndirectedEdge(n_stair2_mid, n_floor2);

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
                          &asset_manager, &nav_graph, &obstacles};

  std::vector<Character *> active_characters;
  active_characters.reserve(1 + enemies.size());

  active_characters.push_back(player.get());
  for (auto &enemy : enemies) {
    active_characters.push_back(enemy.get());
  }

  // 1.5. Evaluate Stealth before AI update so AI can react in the same frame
  stealth_manager.update(active_characters, player.get(), obstacles, dt);

  player->update(ctx);
  for (auto &enemy : enemies) {
    enemy->update(ctx);
  }

  // 2. Resolve Physics Pipeline (4-Step: Gravity -> Integration -> Ejection
  // Loop -> Ground Snap)
  std::vector<Vector3> new_positions =
      physics_manager.updatePhysics(active_characters, obstacles, dt);
  for (size_t i = 0; i < active_characters.size(); ++i) {
    active_characters[i]->setPosition(new_positions[i]);
  }

  // 3. Resolve Combat
  combat_manager.update(active_characters);

  // 4. Update the camera tracking matrix using that position. Built here the
  // same way the UpdateContext above is, so the camera never reaches back into
  // the player for it — and so that the framing, which is a decision about the
  // world rather than about the camera, is made where the world is known.
  CameraFrame shot;
  shot.target = player->getPosition();
  shot.look = input_manager.getRawMouseDelta();
  shot.dt = dt;
  shot.framing =
      player->isDashing() ? CameraFraming::Wide : CameraFraming::Close;
  camera_controller->update(shot);

  if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
    return StateAction::ChangeToMenu;
  }

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
              takedown_text_timer = 2.0f;
              
              pending_aerial_target = nullptr;
          }
      }
  }

  // Takedown logic
  if (!pending_aerial_target && input_manager.isActionPressed(GameAction::Takedown)) {
      for (auto &enemy_ptr : enemies) {
          Enemy* enemy = enemy_ptr.get();
          if (!enemy->getStats().isDead()) {
              Vector3 p_pos = player->getPosition();
              Vector3 e_pos = enemy->getPosition();
              
              Vector2 p2 = {p_pos.x, p_pos.z};
              Vector2 e2 = {e_pos.x, e_pos.z};
              float horiz_dist = Vector2Distance(p2, e2);
              float vert_dist = p_pos.y - e_pos.y;
              
              // Tighter vertical check for grounded takedowns so you don't do grounded takedowns from a ledge
              bool is_normal_range = (horiz_dist < 2.0f && std::abs(vert_dist) < 0.5f);
              bool is_aerial_range = (horiz_dist < 2.5f && vert_dist >= 1.5f && vert_dist < 10.0f && !player->isGrounded());

              if (is_normal_range || is_aerial_range) {
                  // Raycast check to prevent takedowns through walls
                  bool is_blocked = false;
                  Vector3 p_head = { p_pos.x, p_pos.y + player->getColliderHeight() * 0.8f, p_pos.z };
                  Vector3 e_head = { e_pos.x, e_pos.y + enemy->getColliderHeight() * 0.8f, e_pos.z };
                  
                  for (const auto& obs : obstacles) {
                      Vector3 local_obs = Vector3Transform(p_head, obs.getWorldToLocal());
                      Vector3 local_tgt = Vector3Transform(e_head, obs.getWorldToLocal());
                      
                      Ray local_ray;
                      local_ray.position = local_obs;
                      local_ray.direction = Vector3Normalize(Vector3Subtract(local_tgt, local_obs));
                      
                      RayCollision collision = GetRayCollisionBox(local_ray, obs.getLocalBox());
                      if (collision.hit) {
                          float dist_to_tgt_local = Vector3Distance(local_obs, local_tgt);
                          if (collision.distance < dist_to_tgt_local) {
                              is_blocked = true;
                              break;
                          }
                      }
                  }

                  if (is_blocked) continue;

                  bool can_takedown = false;
                  bool is_aerial = is_aerial_range && !is_normal_range;
                  StealthState s_state = enemy->getStealthComponent().getStealthState();

                  // 1. Aerial Takedown
                  if (is_aerial && (s_state == StealthState::Unaware || s_state == StealthState::Suspicious)) {
                      can_takedown = true;
                  }
                  // 2. Stealth Takedown (Must be Unaware or Suspicious, and player closely behind)
                  else if (s_state == StealthState::Unaware || s_state == StealthState::Suspicious) {
                      Vector3 enemy_fwd = {std::sin(enemy->getRotation().y * DEG2RAD), 0.0f, std::cos(enemy->getRotation().y * DEG2RAD)};
                      Vector3 to_player = Vector3Normalize(Vector3Subtract(p_pos, e_pos));
                      float dot = Vector3DotProduct(enemy_fwd, to_player);
                      if (dot < -0.8f) { // Narrower cone (approx 74 degrees directly behind)
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
                          // Do NOT snap X and Z instantly here; it will lerp smoothly in the update loop
                          // Let normal gravity handle the fall instead of boosting it
                      } else {
                          // Snap rotation and position for grounded takedowns
                          if (enemy->getStats().isPostureBroken() && s_state != StealthState::Unaware && s_state != StealthState::Suspicious) {
                              takedown_type_str = "COMBAT DEATHBLOW";
                              // Combat takedown: face the enemy
                              Vector3 to_enemy = Vector3Normalize(Vector3Subtract(e_pos, p_pos));
                              float target_yaw = std::atan2(to_enemy.x, to_enemy.z) * RAD2DEG;
                              player->setRotation({0.0f, target_yaw, 0.0f});
                              
                              // Snap position 1.2 units in front of enemy
                              player->setPosition({e_pos.x - to_enemy.x * 1.2f, p_pos.y, e_pos.z - to_enemy.z * 1.2f});
                          } else {
                              takedown_type_str = "STEALTH TAKEDOWN";
                              // Stealth backstab: align exactly with enemy's facing direction
                              player->setRotation(enemy->getRotation());
                              
                              // Snap position 1.2 units exactly behind the enemy
                              float enemy_yaw = enemy->getRotation().y * DEG2RAD;
                              Vector3 backward = {-std::sin(enemy_yaw), 0.0f, -std::cos(enemy_yaw)};
                              player->setPosition({e_pos.x + backward.x * 1.2f, p_pos.y, e_pos.z + backward.z * 1.2f});
                          }
                          
                          enemy->takeDamage(9999.0f, 0.0f, player.get());
                          player->performTakedown();
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
  ClearBackground(RAYWHITE);
  BeginMode3D(camera_controller->getCamera());

  // Draw all obstacles (walls, ramps, platforms)
  for (const PhysicsObstacle &obs : obstacles) {
    obs.draw();
  }

  renderList.clear();
  renderList.push_back(player->getRenderData());
  for (const auto &enemy : enemies) {
    renderList.push_back(enemy->getRenderData());
  }

  // World + entities, drawn into the 3D scope opened above.
  renderer->renderGameplay(*camera_controller, renderList);

  std::vector<Character *> active_characters;
  active_characters.reserve(1 + enemies.size());
  active_characters.push_back(player.get());
  for (const auto &enemy : enemies) {
    active_characters.push_back(enemy.get());
  }

  // Collision debug wireframes — must stay inside the 3D scope.
  physics_manager.drawDebug(active_characters, obstacles);
  combat_manager.drawDebug(active_characters);
  stealth_manager.drawDebug(active_characters);

  EndMode3D();

  // 2D overlay pass (after the 3D scope is closed).
  renderer->drawUI();

  // --- HEALTH BARS ---
  player->drawHPBar2D();
  for (const auto &enemy : enemies) {
    enemy->drawHPBar(camera_controller->getCamera());
  }

  if (takedown_text_timer > 0.0f) {
      const char* text = takedown_type_str.c_str();
      int font_size = 40;
      int text_width = MeasureText(text, font_size);
      DrawText(text, GetScreenWidth() / 2 - text_width / 2, GetScreenHeight() / 2 - 100, font_size, RED);
  }
}

void GameplayState::exit() {
  // Clean up local menu resources here
}