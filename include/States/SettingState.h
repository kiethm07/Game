#pragma once

#include <States/GameState.h>
#include <raylib.h>
#include <vector>

class Campaign;
class SoundController;
class AssetManager;

class SettingSlider {
public:
  Rectangle bounds{};
  const char *label = nullptr;
  float value = 1.0f;
  bool is_dragging = false;
};

class SettingButton {
public:
  Rectangle bounds{};
  const char *label = nullptr;
  StateAction action = StateAction::KeepCurrent;
};

class SettingState : public GameState {
public:
  SettingState(SoundController &sound_controller, AssetManager &asset_manager,
               Campaign &campaign);
  ~SettingState() override = default;

  SettingState(const SettingState &) = delete;
  SettingState &operator=(const SettingState &) = delete;

  void enter() override;
  StateAction update(float dt) override;
  void draw() override;
  void exit() override;

private:
  void buildUI();
  void drawSlider(const SettingSlider &slider);

  SoundController &sound_controller;
  AssetManager &asset_manager;
  Campaign &campaign;

  SettingSlider bgm_slider;
  SettingSlider sfx_slider;
  std::vector<SettingButton> buttons;
  int hovered_button = -1;
};
