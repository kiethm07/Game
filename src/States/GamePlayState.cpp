#include "States/GamePlayState.h"
#include "raylib.h"
#include <Core/Game.h>

GameplayState::GameplayState(const InputManager &input_manager)
    : input_manager(input_manager) {
  camera_controller = std::make_unique<CameraController>();
  player = std::make_unique<Player>(input_manager);
  enemy = std::make_unique<Enemy>();
  renderer = std::make_unique<GameRenderer>();
}

void GameplayState::enter() {
  // Load local menu-only graphics or titles here
}

StateAction GameplayState::update(float dt) {
  // 1. Tick entities through the shared polymorphic update path. Each reads
  // input/AI internally and shifts its own position safely.
  const UpdateContext ctx{dt, camera_controller->getCameraForward(),
                          camera_controller->getCameraRight()};
  player->update(ctx);
  enemy->update(ctx);
  // 2. Safely spy on the player's new position via the const getter
  Vector3 current_player_pos = player->getPosition();
  Vector3 current_enemy_pos = enemy->getPosition();

  // 3. Update the camera tracking matrix using that position
  Vector2 mouse_delta = input_manager.getRawMouseDelta();
  camera_controller->update(current_player_pos, mouse_delta);

  // State transition handling
  if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
    return StateAction::ChangeToMenu;
  }

  return StateAction::KeepCurrent;
}

void GameplayState::draw() {
  renderList.clear();
  renderList.push_back(player->getRenderData());
  renderList.push_back(enemy->getRenderData());

  renderer->renderGameplay(*camera_controller, renderList);
}

void GameplayState::exit() {
  // Clean up local menu resources here
}