#pragma once

#include <States/GameState.h>
#include <raylib.h>
#include <vector>

class Campaign;
class SoundController;
class AssetManager;

class PauseState : public GameState {
public:
  class PauseButton {
  public:
    Rectangle bounds{};
    const char *label = nullptr;
    StateAction action = StateAction::KeepCurrent;
  };

  PauseState(SoundController &sound_controller, AssetManager &asset_manager,
             Campaign &campaign);
  ~PauseState() override = default;

  PauseState(const PauseState &) = delete;
  PauseState &operator=(const PauseState &) = delete;

  void enter() override;
  StateAction update(float dt) override;
  void draw() override;
  void exit() override;

private:
  void buildButtons();

  SoundController &sound_controller;
  AssetManager &asset_manager;
  Campaign &campaign;

  std::vector<PauseButton> buttons;
  int hovered = -1;
};
