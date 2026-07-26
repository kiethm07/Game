#include "States/GamePlayState.h"
#include "raylib.h"
#include <Core/Game.h>
#include <cassert>

GameplayState::GameplayState(const InputManager &input_manager)
    : input_manager(input_manager) {
  camera_controller = std::make_unique<CameraController>();
  player = std::make_unique<Player>(input_manager);

  // Spawn test enemies via factory
  enemies.push_back(
      EnemyFactory::createEnemy(EnemyType::Swordman, {0.0f, 0.0f, 5.0f}));
  enemies.push_back(
      EnemyFactory::createEnemy(EnemyType::Swordman, {3.0f, 0.0f, 8.0f}));

  // --- MULTI-LEVEL BUILDING EXAMPLE ---
  
  // Floor 1 (Ceiling for Ground)
  // Walk under this (Z=5 to 15) to test flat overhead ceilings!
  obstacles.emplace_back(Vector3{-6.0f, 3.0f, 5.0f}, Vector3{6.0f, 3.5f, 15.0f}, 0.0f, DARKBLUE);

  // Staircase 1: Ground (Y=0) to Floor 1 (Y=3.5)
  // Placed on the left side. Walk under this to test slanted ramp ceilings!
  obstacles.emplace_back(Vector2{-9.0f, -2.0f}, Vector2{-6.0f, 8.0f}, 0.0f, 3.5f, 0.0f, SKYBLUE);

  // Floor 2 (Ceiling for Floor 1)
  obstacles.emplace_back(Vector3{-6.0f, 7.0f, 5.0f}, Vector3{6.0f, 7.5f, 15.0f}, 0.0f, DARKGREEN);

  // Staircase 2: Floor 1 (Y=3.5) to Floor 2 (Y=7.5)
  // Placed on the right side. 
  obstacles.emplace_back(Vector2{6.0f, 5.0f}, Vector2{9.0f, 15.0f}, 3.5f, 7.5f, 0.0f, LIME);

  // Rotated Pillar supporting the center
  obstacles.emplace_back(Vector3{-2.0f, 0.0f, 8.0f}, Vector3{2.0f, 7.0f, 12.0f}, 45.0f, RED);

  // 4 x 4 grid of pillars 
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      obstacles.emplace_back(Vector3{i * 2.0f, 0.0f, j * 2.0f}, Vector3{(i + 1) * 2.0f, 10.0f, (j + 1) * 2.0f}, 0.0f, BLUE);
    }
  }

  // --- THIN WALL FOR TUNNELING TEST ---
  // A wall that is only 0.05 units thick!
  obstacles.emplace_back(Vector3{-5.0f, 0.0f, -5.0f}, Vector3{-4.95f, 5.0f, 5.0f}, 0.0f, MAGENTA);

  renderer = std::make_unique<GameRenderer>();
}

void GameplayState::enter() {
  // Load local menu-only graphics or titles here
}

StateAction GameplayState::update(float dt) {
  // 1. Tick entities through the shared polymorphic update path. Each reads
  // input/AI internally and shifts its own position safely.
  Vector3 player_pos = player->getPosition();
  const UpdateContext ctx{dt, camera_controller->getCameraForward(),
                          camera_controller->getCameraRight(), player_pos};
  player->update(ctx);
  for (auto &enemy : enemies) {
    enemy->update(ctx);
  }

  std::vector<Character *> active_characters;
  active_characters.reserve(1 + enemies.size());

  active_characters.push_back(player.get());
  for (auto &enemy : enemies) {
    active_characters.push_back(enemy.get());
  }

  // 2. Resolve Physics Pipeline (4-Step: Gravity -> Integration -> Ejection
  // Loop -> Ground Snap)
  std::vector<Vector3> new_positions = physics_manager.updatePhysics(active_characters, obstacles, dt);
  for (size_t i = 0; i < active_characters.size(); ++i) {
      active_characters[i]->setPosition(new_positions[i]);
  }

  // 3. Resolve Combat
  combat_manager.update(active_characters);

  // 4. Update the camera tracking matrix using that position
  Vector2 mouse_delta = input_manager.getRawMouseDelta();
  camera_controller->update(player_pos, mouse_delta);

  if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
    return StateAction::ChangeToMenu;
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

  EndMode3D();

  // 2D overlay pass (after the 3D scope is closed).
  renderer->drawUI();

  // --- HEALTH BARS ---
  player->drawHPBar2D();
  for (const auto &enemy : enemies) {
    enemy->drawHPBar(camera_controller->getCamera());
  }
}

void GameplayState::exit() {
  // Clean up local menu resources here
}