#pragma once

#include <States/GameState.h>
#include <cstddef>
#include <raylib.h>
#include <vector>

class Campaign;
class SoundController;
class AssetManager;

class EndGameState : public GameState {
public:
  EndGameState(SoundController &sound_controller, AssetManager &asset_manager,
               Campaign &campaign);
  ~EndGameState() override;

  EndGameState(const EndGameState &) = delete;
  EndGameState &operator=(const EndGameState &) = delete;

  void enter() override;
  StateAction update(float dt) override;
  void draw() override;
  void exit() override;

private:
  class EndButton {
  public:
    Rectangle bounds{};
    const char *label = nullptr;
  };

  void buildButtons();
  void loadBackground();
  void unloadBackground();

  SoundController &sound_controller;
  AssetManager &asset_manager;
  Campaign &campaign;

  Texture2D background{};
  Rectangle bg_dest{};

  std::vector<EndButton> buttons;
  int hovered = -1;
  float alpha_timer = 0.0f;
};
