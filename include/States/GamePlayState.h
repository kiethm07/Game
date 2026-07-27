#pragma once

#include <GameManager/StealthManager.h>

#include <Components/PhysicsObstacle.h>
#include <Core/CameraController.h>
#include <Core/Game.h>
#include <Core/InputManager.h>
#include <Entities/Enemy.h>
#include <Entities/EnemyFactory.h>
#include <Entities/Player.h>
#include <GameManager/CombatManager.h>
#include <GameManager/PhysicsManager.h>
#include <States/GameState.h>

#include <Rendering/GameRenderer.h>
#include <Rendering/RenderData.h>

#include <memory>
#include <vector>

class GameplayState : public GameState {
public:
  GameplayState(const InputManager &input_manager);
  ~GameplayState() override = default;

  void enter() override;
  StateAction update(float dt) override;
  void draw() override;
  void exit() override;

private:
  std::unique_ptr<CameraController> camera_controller;
  std::unique_ptr<Player> player;
  std::vector<std::unique_ptr<Enemy>> enemies;
  std::unique_ptr<GameRenderer> renderer;
  CombatManager combat_manager;
  PhysicsManager physics_manager;
  StealthManager stealth_manager;
  std::vector<PhysicsObstacle> obstacles;
  std::vector<CharacterRenderData> renderList;

  const InputManager &input_manager;
};