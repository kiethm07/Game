#include <States/EndGameState.h>

#include <Core/AssetPaths.h>
#include <GameManager/SoundController.h>
#include <Level/Campaign.h>
#include <Rendering/AssetManager.h>
#include <cmath>
#include <raylib.h>

namespace {

constexpr const char *BACKGROUND_FILE = "endgamescene.png";

constexpr float BUTTON_WIDTH = 0.22f;
constexpr float BUTTON_HEIGHT = 0.06f;
constexpr float FADE_SPEED = 1.2f;

constexpr Color INK_COLOR = {240, 238, 230, 255};
constexpr Color GOLD_ACCENT = {212, 175, 55, 255};
constexpr Color PANEL_BG = {20, 20, 24, 220};
constexpr Color PANEL_HOVER = {45, 45, 54, 240};

} // namespace

EndGameState::EndGameState(SoundController &sound_controller,
                           AssetManager &asset_manager, Campaign &campaign)
    : sound_controller(sound_controller),
      asset_manager(asset_manager),
      campaign(campaign) {}

EndGameState::~EndGameState() {
  unloadBackground();
}

void EndGameState::enter() {
  EnableCursor();
  campaign.markCompleted();
  loadBackground();
  buildButtons();
  alpha_timer = 0.0f;

  asset_manager.loadMusic(AssetID::BGM_VICTORY, assets::path("audio/endgamebgm.mp3"));
  sound_controller.playMusic(AssetID::BGM_VICTORY);
}

void EndGameState::loadBackground() {
  Image image = LoadImage(assets::path(BACKGROUND_FILE).c_str());

  if (IsImageValid(image)) {
    const float screen_w = static_cast<float>(GetScreenWidth());
    const float screen_h = static_cast<float>(GetScreenHeight());

    // Fit entirely inside the screen without cropping (letterbox / black bars)
    const float scale = fminf(screen_w / static_cast<float>(image.width),
                              screen_h / static_cast<float>(image.height));

    const float dest_w = floorf(static_cast<float>(image.width) * scale);
    const float dest_h = floorf(static_cast<float>(image.height) * scale);
    const float dest_x = floorf((screen_w - dest_w) * 0.5f);
    const float dest_y = floorf((screen_h - dest_h) * 0.5f);

    bg_dest = Rectangle{dest_x, dest_y, dest_w, dest_h};

    background = LoadTextureFromImage(image);
    UnloadImage(image);

    if (background.id != 0) {
      SetTextureFilter(background, TEXTURE_FILTER_BILINEAR);
    }
  } else {
    TraceLog(LOG_WARNING, "EndGameState: could not load %s; using flat background",
             BACKGROUND_FILE);
  }
}

void EndGameState::unloadBackground() {
  if (background.id != 0) {
    UnloadTexture(background);
    background = Texture2D{};
  }
}

void EndGameState::buildButtons() {
  const float screen_w = static_cast<float>(GetScreenWidth());
  const float screen_h = static_cast<float>(GetScreenHeight());

  const float bw = screen_w * BUTTON_WIDTH;
  const float bh = screen_h * BUTTON_HEIGHT;
  const float bx = (screen_w - bw) * 0.5f;
  const float by = screen_h * 0.88f;

  buttons.clear();
  EndButton return_btn;
  return_btn.bounds = Rectangle{bx, by, bw, bh};
  return_btn.label = "RETURN TO MENU";
  buttons.push_back(return_btn);

  hovered = -1;
}

StateAction EndGameState::update(float dt) {
  alpha_timer += dt * FADE_SPEED;
  if (alpha_timer > 1.0f) {
    alpha_timer = 1.0f;
  }

  const Vector2 mouse = GetMousePosition();
  hovered = -1;

  for (size_t i = 0; i < buttons.size(); ++i) {
    if (CheckCollisionPointRec(mouse, buttons[i].bounds)) {
      hovered = static_cast<int>(i);
      break;
    }
  }

  if (hovered >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    return StateAction::ChangeToMenu;
  }

  if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ESCAPE)) {
    return StateAction::ChangeToMenu;
  }

  return StateAction::KeepCurrent;
}

void EndGameState::draw() {
  ClearBackground(BLACK);

  const float current_alpha = alpha_timer;

  if (background.id != 0) {
    const Rectangle src_rec = {0.0f, 0.0f, static_cast<float>(background.width), static_cast<float>(background.height)};
    const Color tint = Color{255, 255, 255, static_cast<unsigned char>(255 * current_alpha)};
    DrawTexturePro(background, src_rec, bg_dest, Vector2{0.0f, 0.0f}, 0.0f, tint);
  }

  // Interactive buttons
  for (size_t i = 0; i < buttons.size(); ++i) {
    const EndButton &btn = buttons[i];
    Color fill_color = PANEL_BG;
    if (static_cast<int>(i) == hovered) {
      fill_color = PANEL_HOVER;
    }

    fill_color.a = static_cast<unsigned char>(fill_color.a * current_alpha);
    DrawRectangleRec(btn.bounds, fill_color);

    Color border_color = Color{GOLD_ACCENT.r, GOLD_ACCENT.g, GOLD_ACCENT.b, static_cast<unsigned char>(140 * current_alpha)};
    if (static_cast<int>(i) == hovered) {
      border_color = Color{GOLD_ACCENT.r, GOLD_ACCENT.g, GOLD_ACCENT.b, static_cast<unsigned char>(255 * current_alpha)};
    }
    DrawRectangleLinesEx(btn.bounds, 1.5f, border_color);

    const int font_size = static_cast<int>(btn.bounds.height * 0.42f);
    const int text_w = MeasureText(btn.label, font_size);
    const int text_x = static_cast<int>(btn.bounds.x + (btn.bounds.width - text_w) * 0.5f);
    const int text_y = static_cast<int>(btn.bounds.y + (btn.bounds.height - font_size) * 0.5f);

    Color btn_text_color = INK_COLOR;
    if (static_cast<int>(i) == hovered) {
      btn_text_color = GOLD_ACCENT;
    }
    btn_text_color.a = static_cast<unsigned char>(255 * current_alpha);

    DrawText(btn.label, text_x, text_y, font_size, btn_text_color);
  }
}

void EndGameState::exit() {
  unloadBackground();
}
