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

  /// Put the first state on the stack.
  ///
  /// Separate from the constructor, and it has to be. Game is a member of
  /// Application, so it is fully constructed before Application's *body* runs
  /// InitWindow and assets::resolve() -- and MainMenuState::enter() loads a
  /// texture, which needs both a GL context and a resolved asset root. Call
  /// once, after those two, before run().
  void start();

  void update();
  void draw();

  void pushState(std::unique_ptr<GameState> state);
  void popState();
  void changeState(std::unique_ptr<GameState> state);

  bool isEmpty() const { return states.empty(); }

  /// Has a state asked the program to end?
  ///
  /// Read by Application's loop alongside WindowShouldClose(). This is what
  /// makes StateAction::RequestQuit mean something: it used to land in an empty
  /// if-body, and quitting worked only because ESC happened to be raylib's
  /// default exit key.
  bool shouldQuit() const { return quit_requested; }

  // InputManager& GetInputManager() { return inputManager; }

private:
  /// The only place a MainMenuState is built.
  ///
  /// It needs `campaign`, which is private, and that is exactly why this is a
  /// method rather than three call sites: Application does not get an accessor
  /// to a service just to hand it straight back. Three construction sites
  /// became one, so the next argument the menu grows is a one-line edit.
  void pushMainMenu();

  /// Set by StateAction::RequestQuit and never cleared -- a run that has asked
  /// to end does not un-ask.
  bool quit_requested = false;

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