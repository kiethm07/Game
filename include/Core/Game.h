#pragma once
#include <CombatData/AttackRegistry.h>
#include <Core/InputManager.h>
#include <Level/Campaign.h>
#include <Core/TimeManager.h>
#include <GameManager/SoundController.h>
#include <Rendering/AssetManager.h>
#include <States/GamePlayState.h>
#include <States/GameState.h>
#include <States/MainMenuState.h>
#include <memory>
#include <vector>

class Game {
public:
  Game();
  ~Game();

  Game(const Game &) = delete;
  Game &operator=(const Game &) = delete;

  void update();
  void draw();

  void pushState(std::unique_ptr<GameState> state);
  void popState();
  void changeState(std::unique_ptr<GameState> state);

  bool isEmpty() const { return states.empty(); }

  // InputManager& GetInputManager() { return inputManager; }

private:
  InputManager input_manager;
  TimeManager time_manager;
  AssetManager asset_manager;
  SoundController sound_controller;
  AttackRegistry &attack_registry;

  /// Where the run is and what it is carrying. Declared with the other
  /// services and before `states` for the same reason they are: states hold
  /// references to it and must be destroyed first.
  Campaign campaign;

  std::vector<std::unique_ptr<GameState>> states;
};