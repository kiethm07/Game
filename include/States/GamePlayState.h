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
#include <Level/Campaign.h>
#include <Level/Level.h>
#include <Physics/CollisionMesh.h>
#include <States/GameState.h>

#include <Rendering/GameRenderer.h>
#include <Rendering/RenderData.h>

#include <memory>
#include <vector>
#include <string>

#include <GameManager/SoundController.h>

struct MoneyDrop {
    Vector3 position;
    int amount;
    float bob_timer;
};

class GameplayState : public GameState {
public:
  GameplayState(const InputManager &input_manager, AssetManager &asset_manager,
                SoundController &sound_controller, Campaign &campaign);
  ~GameplayState() override = default;

  void enter() override;
  StateAction update(float dt) override;
  void draw() override;
  void exit() override;

private:
  /// Declared before `renderer`, which holds a reference to it: destruction is
  /// reverse-declaration order, so the renderer dies first.
  AssetManager& asset_manager;

  /// The loaded map. Also declared before `renderer`, which reads its visual
  /// mesh path and bounds during construction.
  Level level;

  /// Triangle collision for the parts of the world BOX_/RAMP_ proxies cannot
  /// express. Empty for levels that ship none (greybox, forest), and every
  /// consumer has to tolerate that rather than assume a mesh is present.
  CollisionMesh collision_mesh;

  std::unique_ptr<CameraController> camera_controller;
  std::unique_ptr<Player> player;
  std::vector<std::unique_ptr<Enemy>> enemies;
  std::unique_ptr<GameRenderer> renderer;
  CombatManager combat_manager;
  PhysicsManager physics_manager;
  StealthManager stealth_manager;
  std::vector<SmokeCloud> smoke_clouds;
  std::vector<MoneyDrop> money_drops;
  ParticleManager particle_manager;
  std::vector<CharacterRenderData> renderList;

  const InputManager &input_manager;
  SoundController &sound_controller;

  /// Which phase this is, and the run's carry. Read in the constructor to
  /// choose the level and restore the player, written when the phase is left.
  /// A reference to a Game member, like the three above it -- states are
  /// destroyed before the services they point at.
  Campaign &campaign;

  /// The money and item charges this phase would hand to the next one.
  /// Read off the live Player, so only meaningful while one exists.
  PhaseCarry snapshotCarry() const;

  /// The F4 readout: a paste-ready enemies.json line, shown for a few seconds.
  /// Latched rather than drawn while a key is held, because the whole point is
  /// to press it and then go and read it somewhere else.
  std::string spawn_line;
  float spawn_line_timer = 0.0f;

  /// Which type F4 writes into that line. SHIFT+F4 cycles it.
  EnemyType debug_spawn_type = EnemyType::Swordman;
  
  float takedown_text_timer = 0.0f;
  std::string takedown_type_str = "";
  float smoke_cooldown_timer = 0.0f;
  
  NavMeshBuilder nav_builder;
  NavMeshQuery nav_query;

  /// F1: overlay the raw shadow depth map in the corner.
  bool show_shadow_map = false;

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