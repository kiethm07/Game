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

  // Spawn sample walls
  walls.emplace_back(Vector3{-10.0f, 0.0f, 15.0f}, Vector3{10.0f, 3.0f, 16.0f},
                     DARKGRAY);
  walls.emplace_back(Vector3{5.0f, 0.0f, -3.0f}, Vector3{7.0f, 3.0f, -1.0f},
                     GRAY);
  walls.emplace_back(Vector3{-7.0f, 0.0f, 3.0f}, Vector3{-5.0f, 3.0f, 5.0f},
                     GRAY);

  // Spawn sample terrain (Slope / Ramp, Cliff Plateau, Stepped Platform)
  terrain.addRamp(TerrainRamp({-3.0f, 2.0f}, {3.0f, 10.0f}, 0.0f, 3.0f, BEIGE));
  terrain.addPlatform(
      TerrainPlatform({-5.0f, 0.0f, 10.0f}, {5.0f, 3.0f, 18.0f}, BROWN));
  terrain.addPlatform(
      TerrainPlatform({-12.0f, 0.0f, -6.0f}, {-6.0f, 1.5f, -2.0f}, DARKBROWN));

  // Plant every entity on the terrain surface at its spawn column so nobody
  // starts embedded inside a ramp/platform (which the wall solver would eject).
  auto snapToGround = [this](Character *c) {
    Vector3 p = c->getPosition();
    // Large step/head-room so spawn picks up whatever surface is in the column,
    // however far below the feet it currently sits.
    GroundSample g = terrain.sampleGround(p, 1e6f, 1e6f, p.y);
    p.y = g.height;
    c->setPosition(p);
    c->setGroundReferenceY(g.height);
  };
  snapToGround(player.get());
  for (auto &enemy : enemies) {
    snapToGround(enemy.get());
  }

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

  // 3. Resolve Physics Pipeline (4-Step: Gravity -> Integration -> Ejection
  // Loop -> Ground Snap)
  physics_manager.updatePhysics(active_characters, terrain, walls, dt);

  // 4. Resolve Combat
  combat_manager.update(active_characters);

  // 3. Update the camera tracking matrix using that position
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

  terrain.draw();

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
  physics_manager.drawDebug(active_characters, walls);
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