#pragma once

#include <GameManager/StealthManager.h>
#include <GameManager/SmokeCloud.h>
#include <Rendering/ParticleManager.h>

#include <Components/PhysicsObstacle.h>
#include <Core/CameraController.h>
#include <Core/Game.h>
#include <Core/InputManager.h>
#include <Entities/Enemy.h>
#include <Entities/EnemyFactory.h>
#include <Entities/Player.h>
#include <GameManager/CombatManager.h>
#include <GameManager/PhysicsManager.h>
#include <AI/NavMeshBuilder.h>
#include <AI/NavMeshQuery.h>
#include <States/GameState.h>

#include <Rendering/GameRenderer.h>
#include <Rendering/RenderData.h>

#include <memory>
#include <vector>
#include <string>

struct MoneyDrop {
    Vector3 position;
    int amount;
    float bob_timer;
};

class GameplayState : public GameState {
public:
  GameplayState(const InputManager &input_manager, AssetManager &asset_manager);
  ~GameplayState() override = default;

  void enter() override;
  StateAction update(float dt) override;
  void draw() override;
  void exit() override;

private:
  /// Declared before `renderer`, which holds a reference to it: destruction is
  /// reverse-declaration order, so the renderer dies first.
  AssetManager& asset_manager;

  std::unique_ptr<CameraController> camera_controller;
  std::unique_ptr<Player> player;
  std::vector<std::unique_ptr<Enemy>> enemies;
  std::unique_ptr<GameRenderer> renderer;
  CombatManager combat_manager;
  PhysicsManager physics_manager;
  StealthManager stealth_manager;
  std::vector<PhysicsObstacle> obstacles;
  std::vector<SmokeCloud> smoke_clouds;
  std::vector<MoneyDrop> money_drops;
  ParticleManager particle_manager;
  std::vector<CharacterRenderData> renderList;

  const InputManager &input_manager;
  
  float takedown_text_timer = 0.0f;
  std::string takedown_type_str = "";
  float smoke_cooldown_timer = 0.0f;
  
  NavMeshBuilder nav_builder;
  NavMeshQuery nav_query;

  /// Used strictly for debugging takedown mechanics across frames.
  Character *pending_aerial_target = nullptr;

  /// Who is being executed, held for as long as the swing runs so the camera
  /// has a second point to compose its shot around. Cleared by the camera step
  /// once Player::isExecuting() goes false, so a victim outliving the animation
  /// is not possible. Safe to hold raw: enemies are owned by `enemies` for the
  /// whole life of the state and are never erased, only killed.
  Character *deathblow_victim = nullptr;
  Character *locked_target = nullptr;
};