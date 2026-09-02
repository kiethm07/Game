#include <States/SettingState.h>

#include <GameManager/SoundController.h>
#include <Level/Campaign.h>
#include <Rendering/AssetManager.h>
#include <cmath>
#include <cstdio>
#include <raylib.h>

namespace {

constexpr float SLIDER_WIDTH = 0.38f;
constexpr float SLIDER_HEIGHT = 0.075f;
constexpr float BUTTON_WIDTH = 0.20f;
constexpr float BUTTON_HEIGHT = 0.06f;

constexpr Color INK_COLOR = {235, 232, 225, 255};
constexpr Color GOLD_ACCENT = {212, 175, 55, 255};
constexpr Color PANEL_BG = {20, 20, 24, 230};
constexpr Color PANEL_HOVER = {45, 45, 56, 245};
constexpr Color TRACK_BG = {40, 40, 48, 255};

} // namespace

SettingState::SettingState(SoundController &sound_controller,
                           AssetManager &asset_manager, Campaign &campaign)
    : sound_controller(sound_controller),
      asset_manager(asset_manager),
      campaign(campaign) {}

void SettingState::enter() {
  EnableCursor();
  bgm_slider.value = sound_controller.getMusicVolume();
  sfx_slider.value = sound_controller.getSFXVolume();
  buildUI();
}

void SettingState::buildUI() {
  const float screen_w = static_cast<float>(GetScreenWidth());
  const float screen_h = static_cast<float>(GetScreenHeight());

  const float sw = screen_w * SLIDER_WIDTH;
  const float sh = screen_h * SLIDER_HEIGHT;
  const float sx = (screen_w - sw) * 0.5f;

  const float bgm_y = screen_h * 0.40f;
  const float sfx_y = screen_h * 0.51f;

  bgm_slider.bounds = Rectangle{sx, bgm_y, sw, sh};
  bgm_slider.label = "BGM";
  bgm_slider.is_dragging = false;

  sfx_slider.bounds = Rectangle{sx, sfx_y, sw, sh};
  sfx_slider.label = "SFX";
  sfx_slider.is_dragging = false;

  const float bw = screen_w * BUTTON_WIDTH;
  const float bh = screen_h * BUTTON_HEIGHT;
  const float bx = (screen_w - bw) * 0.5f;
  const float by = screen_h * 0.66f;

  buttons.clear();

  SettingButton back_btn;
  back_btn.bounds = Rectangle{bx, by, bw, bh};
  back_btn.label = "BACK";
  back_btn.action = StateAction::PopSetting;
  buttons.push_back(back_btn);

  hovered_button = -1;
}

StateAction SettingState::update(float dt) {
  (void)dt;

  if (IsKeyPressed(KEY_ESCAPE)) {
    return StateAction::PopSetting;
  }

  const Vector2 mouse = GetMousePosition();

  // BGM Slider update
  const float bgm_track_x = bgm_slider.bounds.x + 90.0f;
  const float bgm_track_w = bgm_slider.bounds.width - 160.0f;

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    if (CheckCollisionPointRec(mouse, bgm_slider.bounds)) {
      bgm_slider.is_dragging = true;
    }
  }

  if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
    bgm_slider.is_dragging = false;
  }

  if (bgm_slider.is_dragging) {
    float ratio = (mouse.x - bgm_track_x) / bgm_track_w;
    if (ratio < 0.0f) {
      ratio = 0.0f;
    }
    if (ratio > 1.0f) {
      ratio = 1.0f;
    }
    bgm_slider.value = ratio;
    sound_controller.setMusicVolume(ratio);
  }

  // SFX Slider update
  const float sfx_track_x = sfx_slider.bounds.x + 90.0f;
  const float sfx_track_w = sfx_slider.bounds.width - 160.0f;

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    if (CheckCollisionPointRec(mouse, sfx_slider.bounds)) {
      sfx_slider.is_dragging = true;
    }
  }

  if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
    if (sfx_slider.is_dragging) {
      sound_controller.playSFX(AssetID::SFX_SLASH);
    }
    sfx_slider.is_dragging = false;
  }

  if (sfx_slider.is_dragging) {
    float ratio = (mouse.x - sfx_track_x) / sfx_track_w;
    if (ratio < 0.0f) {
      ratio = 0.0f;
    }
    if (ratio > 1.0f) {
      ratio = 1.0f;
    }
    sfx_slider.value = ratio;
    sound_controller.setSFXVolume(ratio);
  }

  // Back button update
  hovered_button = -1;
  for (size_t i = 0; i < buttons.size(); ++i) {
    if (CheckCollisionPointRec(mouse, buttons[i].bounds)) {
      hovered_button = static_cast<int>(i);
      break;
    }
  }

  if (hovered_button >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    return buttons[hovered_button].action;
  }

  return StateAction::KeepCurrent;
}

void SettingState::drawSlider(const SettingSlider &slider) {
  const Rectangle bounds = slider.bounds;
  const Vector2 mouse = GetMousePosition();
  const bool is_hovered = CheckCollisionPointRec(mouse, bounds) || slider.is_dragging;

  Color panel_color = PANEL_BG;
  Color border_color = Fade(GOLD_ACCENT, 0.35f);
  if (is_hovered) {
    panel_color = PANEL_HOVER;
    border_color = Fade(GOLD_ACCENT, 0.85f);
  }

  DrawRectangleRec(bounds, panel_color);
  DrawRectangleLinesEx(bounds, 1.5f, border_color);

  // Label text on the left
  const int label_size = static_cast<int>(bounds.height * 0.38f);
  const int label_x = static_cast<int>(bounds.x + 20.0f);
  const int label_y = static_cast<int>(bounds.y + (bounds.height - label_size) * 0.5f);
  DrawText(slider.label, label_x, label_y, label_size, GOLD_ACCENT);

  // Track bar geometry
  const float track_x = bounds.x + 90.0f;
  const float track_w = bounds.width - 160.0f;
  const float track_h = 8.0f;
  const float track_y = bounds.y + (bounds.height - track_h) * 0.5f;

  // Background track
  DrawRectangle(static_cast<int>(track_x), static_cast<int>(track_y),
                 static_cast<int>(track_w), static_cast<int>(track_h), TRACK_BG);

  // Active progress fill
  const float fill_w = track_w * slider.value;
  DrawRectangle(static_cast<int>(track_x), static_cast<int>(track_y),
                 static_cast<int>(fill_w), static_cast<int>(track_h), GOLD_ACCENT);

  // Handle knob
  const float handle_x = track_x + fill_w;
  const float handle_y = bounds.y + bounds.height * 0.5f;
  float handle_radius = 7.5f;
  if (slider.is_dragging) {
    handle_radius = 9.0f;
  }

  DrawCircle(static_cast<int>(handle_x), static_cast<int>(handle_y), handle_radius, GOLD_ACCENT);
  DrawCircle(static_cast<int>(handle_x), static_cast<int>(handle_y), handle_radius - 2.0f, INK_COLOR);

  // Percentage text on the right
  char pct_buffer[16];
  snprintf(pct_buffer, sizeof(pct_buffer), "%d%%", static_cast<int>(roundf(slider.value * 100.0f)));
  const int pct_size = static_cast<int>(bounds.height * 0.34f);
  const int pct_x = static_cast<int>(bounds.x + bounds.width - 55.0f);
  const int pct_y = static_cast<int>(bounds.y + (bounds.height - pct_size) * 0.5f);
  DrawText(pct_buffer, pct_x, pct_y, pct_size, INK_COLOR);
}

void SettingState::draw() {
  const int screen_w = GetScreenWidth();
  const int screen_h = GetScreenHeight();

  // Dark overlay
  DrawRectangle(0, 0, screen_w, screen_h, Fade(BLACK, 0.75f));

  // "SETTINGS" Title
  const int title_size = static_cast<int>(screen_h * 0.08f);
  const char *title_text = "SETTINGS";
  const int title_w = MeasureText(title_text, title_size);
  const int title_x = (screen_w - title_w) / 2;
  const int title_y = static_cast<int>(screen_h * 0.22f);

  DrawText(title_text, title_x + 3, title_y + 3, title_size, Fade(BLACK, 0.8f));
  DrawText(title_text, title_x, title_y, title_size, GOLD_ACCENT);

  const int line_w = static_cast<int>(title_w * 1.5f);
  const int line_x = (screen_w - line_w) / 2;
  const int line_y = title_y + title_size + 12;
  DrawRectangle(line_x, line_y, line_w, 2, Fade(GOLD_ACCENT, 0.6f));

  // Sliders
  drawSlider(bgm_slider);
  drawSlider(sfx_slider);

  // Back button
  for (size_t i = 0; i < buttons.size(); ++i) {
    const SettingButton &btn = buttons[i];
    Color fill_color = PANEL_BG;
    Color border_color = Fade(GOLD_ACCENT, 0.4f);
    Color text_color = INK_COLOR;

    if (static_cast<int>(i) == hovered_button) {
      fill_color = PANEL_HOVER;
      border_color = GOLD_ACCENT;
      text_color = GOLD_ACCENT;
    }

    DrawRectangleRec(btn.bounds, fill_color);
    DrawRectangleLinesEx(btn.bounds, 1.5f, border_color);

    const int font_size = static_cast<int>(btn.bounds.height * 0.40f);
    const int text_w = MeasureText(btn.label, font_size);
    const int text_x = static_cast<int>(btn.bounds.x + (btn.bounds.width - text_w) * 0.5f);
    const int text_y = static_cast<int>(btn.bounds.y + (btn.bounds.height - font_size) * 0.5f);

    DrawText(btn.label, text_x, text_y, font_size, text_color);
  }
}

void SettingState::exit() {
  // Return cursor state to previous screen
}
