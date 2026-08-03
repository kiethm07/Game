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
#include <AI/NavGraph.h>
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
  /// Declared before `renderer`, which holds a reference to it: destruction is
  /// reverse-declaration order, so the renderer dies first.
  AssetManager asset_manager;

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
  
  float takedown_text_timer = 0.0f;
  std::string takedown_type_str = "";
  
  NavGraph nav_graph;

  /// Used strictly for debugging takedown mechanics across frames.
  Character *pending_aerial_target = nullptr;
};