#include <States/PauseState.h>

#include <GameManager/SoundController.h>
#include <Level/Campaign.h>
#include <Rendering/AssetManager.h>
#include <raylib.h>

namespace {

constexpr float BUTTON_WIDTH = 0.24f;
constexpr float BUTTON_HEIGHT = 0.065f;
constexpr float BUTTON_GAP = 0.02f;

constexpr Color INK_COLOR = {235, 232, 225, 255};
constexpr Color GOLD_ACCENT = {212, 175, 55, 255};
constexpr Color PANEL_BG = {20, 20, 24, 230};
constexpr Color PANEL_HOVER = {45, 45, 56, 245};

} // namespace

PauseState::PauseState(SoundController &sound_controller,
                       AssetManager &asset_manager, Campaign &campaign)
    : sound_controller(sound_controller),
      asset_manager(asset_manager),
      campaign(campaign) {}

void PauseState::enter() {
  EnableCursor();
  buildButtons();
}

void PauseState::buildButtons() {
  const float screen_w = static_cast<float>(GetScreenWidth());
  const float screen_h = static_cast<float>(GetScreenHeight());

  const float bw = screen_w * BUTTON_WIDTH;
  const float bh = screen_h * BUTTON_HEIGHT;
  const float bgap = screen_h * BUTTON_GAP;
  const float bx = (screen_w - bw) * 0.5f;

  const float total_h = 4.0f * bh + 3.0f * bgap;
  const float start_y = (screen_h - total_h) * 0.55f;

  buttons.clear();

  PauseButton resume_btn;
  resume_btn.bounds = Rectangle{bx, start_y, bw, bh};
  resume_btn.label = "RESUME";
  resume_btn.action = StateAction::PopPause;
  buttons.push_back(resume_btn);

  PauseButton retry_btn;
  retry_btn.bounds = Rectangle{bx, start_y + (bh + bgap), bw, bh};
  retry_btn.label = "RETRY CHAPTER";
  retry_btn.action = StateAction::RequestReloadPhase;
  buttons.push_back(retry_btn);

  PauseButton menu_btn;
  menu_btn.bounds = Rectangle{bx, start_y + 2.0f * (bh + bgap), bw, bh};
  menu_btn.label = "RETURN TO MENU";
  menu_btn.action = StateAction::ChangeToMenu;
  buttons.push_back(menu_btn);

  PauseButton quit_btn;
  quit_btn.bounds = Rectangle{bx, start_y + 3.0f * (bh + bgap), bw, bh};
  quit_btn.label = "QUIT GAME";
  quit_btn.action = StateAction::RequestQuit;
  buttons.push_back(quit_btn);

  hovered = -1;
}

StateAction PauseState::update(float dt) {
  (void)dt;

  if (IsKeyPressed(KEY_ESCAPE)) {
    return StateAction::PopPause;
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
    return buttons[hovered].action;
  }

  return StateAction::KeepCurrent;
}

void PauseState::draw() {
  const int screen_w = GetScreenWidth();
  const int screen_h = GetScreenHeight();

  // Dark frosted overlay on top of frozen game screen
  DrawRectangle(0, 0, screen_w, screen_h, Fade(BLACK, 0.70f));

  // "PAUSED" title banner
  const int title_size = static_cast<int>(screen_h * 0.08f);
  const char *title_text = "PAUSED";
  const int title_w = MeasureText(title_text, title_size);
  const int title_x = (screen_w - title_w) / 2;
  const int title_y = static_cast<int>(screen_h * 0.22f);

  // Drop shadow and gold title
  DrawText(title_text, title_x + 3, title_y + 3, title_size, Fade(BLACK, 0.8f));
  DrawText(title_text, title_x, title_y, title_size, GOLD_ACCENT);

  // Accent divider line
  const int line_w = static_cast<int>(title_w * 1.5f);
  const int line_x = (screen_w - line_w) / 2;
  const int line_y = title_y + title_size + 12;
  DrawRectangle(line_x, line_y, line_w, 2, Fade(GOLD_ACCENT, 0.6f));

  // Interactive buttons
  for (size_t i = 0; i < buttons.size(); ++i) {
    const PauseButton &btn = buttons[i];
    Color fill_color = PANEL_BG;
    Color border_color = Fade(GOLD_ACCENT, 0.4f);
    Color text_color = INK_COLOR;

    if (static_cast<int>(i) == hovered) {
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

void PauseState::exit() {
  DisableCursor();
}
