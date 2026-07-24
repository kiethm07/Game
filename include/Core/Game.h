#pragma once
#include <CombatData/AttackRegistry.h>
#include <Core/InputManager.h>
#include <Core/TimeManager.h>
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
  AttackRegistry &attack_registry;
  std::vector<std::unique_ptr<GameState>> states;
};