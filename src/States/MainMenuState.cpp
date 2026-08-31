#include <States/MainMenuState.h>

#include <Core/AssetPaths.h>
#include <GameManager/SoundController.h>
#include <Level/Campaign.h>
#include <Rendering/AssetManager.h>
#include <cmath>
#include <raylib.h>

namespace {

/// Cover photo assets
constexpr const char *BG_DEFAULT = "mainmenuphoto.jpg";
constexpr const char *BG_RETURN = "mainmenureturn.png";

// --- Layout ----------------------------------------------------------------
//
// The photo is a moonlit scene with the moon centre-right, the red maple
// upper-centre and the samurai at about 80% across. The left third is dark,
// low-detail mist, so that is where the readable UI can go without covering any
// of it. The thirds boundary at 1366 wide is 455; the button block ends at 392.
//
// Fractions of the screen, not pixels, because the old menu hardcoded 100/200/
// 260/290 and would have put its text through the samurai on any other size.
constexpr float kMarginX = 0.0527f;     // 72 px at 1366
constexpr float kButtonW = 0.2343f;     // 320 px
constexpr float kButtonH = 0.0729f;     // 56 px at 768
constexpr float kButtonGap = 0.0286f;   // 22 px
constexpr float kStackTop = 0.53f;      // 407 px
constexpr float kScrimW = 0.42f;        // fades out well before the moon

/// Bone white, off the photo's own moon rather than pure #FFFFFF.
constexpr Color kInk = {235, 232, 225, 255};

/// Roman numerals for the chapter labels.
///
/// Table-driven to X rather than a general algorithm, and falls back to the
/// decimal index past that. The campaign is three phases long; the fallback is
/// only here so that a manifest that grows past ten rows produces a slightly
/// ugly button instead of a blank one.
const char *roman(size_t n) {
  static const char *kNumerals[] = {"I",    "II",   "III", "IV", "V",
                                    "VI",   "VII",  "VIII", "IX", "X"};
  if (n >= 1 && n <= 10) return kNumerals[n - 1];
  return TextFormat("%d", static_cast<int>(n));
}

/// The centred crop of a `tw`x`th` texture that exactly fills `sw`x`sh`.
///
/// floorf rather than a bare halving: covering 1366x768 with 1366x781 leaves an
/// offset of 6.5, and a half-texel source origin resamples the entire screen
/// off-grid instead of blitting it 1:1.
Rectangle coverSource(int tw, int th, int sw, int sh) {
  return Rectangle{floorf((tw - sw) * 0.5f), floorf((th - sh) * 0.5f),
                   static_cast<float>(sw), static_cast<float>(sh)};
}

} // namespace

MainMenuState::MainMenuState(SoundController &sound_controller,
                           AssetManager &asset_manager, Campaign &campaign)
    : sound_controller(sound_controller),
      asset_manager(asset_manager),
      campaign(campaign) {}

MainMenuState::~MainMenuState() {
  // exit() has already run on every path Game takes (popState calls it), so
  // this is belt and braces for a state destroyed without one.
  unloadBackground();
}

void MainMenuState::enter() {
  // The menu is driven by the mouse, so the cursor has to be free here.
  //
  // Application no longer disables it at startup, precisely because that call
  // ran *after* this one: the first menu came up with an invisible cursor
  // locked to the window centre and no button could ever be hovered. Each state
  // now sets its own mode in enter(), and since every transition in
  // Game::update is pop-then-push, the incoming state's enter() is always last.
  EnableCursor();

  loadBackground();
  buildButtons();

  if (campaign.isCompleted()) {
    asset_manager.loadMusic(AssetID::BGM_RETURN, assets::path("audio/mainmenubgm2.mp3"));
    sound_controller.playMusic(AssetID::BGM_RETURN);
  } else {
    asset_manager.loadMusic(AssetID::BGM_MENU, assets::path("audio/mainmenubgm.mp3"));
    sound_controller.playMusic(AssetID::BGM_MENU);
  }
}

void MainMenuState::loadBackground() {
  const float screen_w = static_cast<float>(GetScreenWidth());
  const float screen_h = static_cast<float>(GetScreenHeight());

  if (campaign.isCompleted()) {
    // Comeback background (mainmenureturn.png): DO NOT crop, fit uncropped (letterbox/black bars)
    Image image = LoadImage(assets::path(BG_RETURN).c_str());
    if (IsImageValid(image)) {
      const float scale = fminf(screen_w / static_cast<float>(image.width),
                                screen_h / static_cast<float>(image.height));

      const float dest_w = floorf(static_cast<float>(image.width) * scale);
      const float dest_h = floorf(static_cast<float>(image.height) * scale);
      const float dest_x = floorf((screen_w - dest_w) * 0.5f);
      const float dest_y = floorf((screen_h - dest_h) * 0.5f);

      bg_dest = Rectangle{dest_x, dest_y, dest_w, dest_h};

      background = LoadTextureFromImage(image);
      if (background.id != 0) {
        bg_source = Rectangle{0.0f, 0.0f, static_cast<float>(background.width),
                              static_cast<float>(background.height)};
        SetTextureFilter(background, TEXTURE_FILTER_BILINEAR);
      }
    }
    UnloadImage(image);

    if (background.id == 0) {
      TraceLog(LOG_WARNING,
               "MainMenuState: could not load '%s'; the menu will come up on a "
               "flat background. The chapter buttons still work.",
               BG_RETURN);
    }
  } else {
    // Original background (mainmenuphoto.jpg): CROP to cover full screen
    Image image = LoadImage(assets::path(BG_DEFAULT).c_str());
    if (IsImageValid(image)) {
      const float scale = fmaxf(screen_w / static_cast<float>(image.width),
                                screen_h / static_cast<float>(image.height));
      ImageResize(&image, static_cast<int>(ceilf(image.width * scale)),
                  static_cast<int>(ceilf(image.height * scale)));

      background = LoadTextureFromImage(image);
      if (background.id != 0) {
        bg_source = coverSource(background.width, background.height,
                                static_cast<int>(screen_w), static_cast<int>(screen_h));
        bg_dest = Rectangle{0.0f, 0.0f, screen_w, screen_h};
      }
    }
    UnloadImage(image);

    if (background.id == 0) {
      TraceLog(LOG_WARNING,
               "MainMenuState: could not load '%s'; the menu will come up on a "
               "flat background. The chapter buttons still work.",
               BG_DEFAULT);
    }
  }
}

void MainMenuState::unloadBackground() {
  UnloadTexture(background); // a no-op when id == 0
  background = Texture2D{};  // so a second call cannot double-free
}

void MainMenuState::buildButtons() {
  const float screen_w = static_cast<float>(GetScreenWidth());
  const float screen_h = static_cast<float>(GetScreenHeight());

  const float x = screen_w * kMarginX;
  const float w = screen_w * kButtonW;
  const float h = screen_h * kButtonH;
  const float pitch = h + screen_h * kButtonGap;

  // One button per manifest row, never a hardcoded three. CampaignManifest.h
  // promises that adding a phase is one row and nothing else; a fixed count
  // here would break that promise the day a fourth row lands, and break it
  // silently -- the phase would exist, load fine, and simply be unreachable.
  buttons.clear();
  buttons.reserve(campaign.count());
  for (size_t i = 0; i < campaign.count(); ++i) {
    buttons.push_back(MenuButton{
        Rectangle{x, screen_h * kStackTop + i * pitch, w, h}, i});
  }
}

StateAction MainMenuState::update(float dt) {
  (void)dt; // nothing here is animated

  const Vector2 mouse = GetMousePosition();

  hovered = -1;
  for (size_t i = 0; i < buttons.size(); ++i) {
    if (CheckCollisionPointRec(mouse, buttons[i].bounds)) {
      hovered = static_cast<int>(i);
      break;
    }
  }

  if (hovered >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    const size_t phase = buttons[hovered].phase;
    if (!campaign.startAt(phase)) {
      // Only reachable if `buttons` and kCampaignPhases have gone out of step,
      // which buildButtons() makes impossible today. Refuse rather than start
      // some other phase, and say so.
      TraceLog(LOG_ERROR,
               "MainMenuState: button %d names phase %d, which is not a row in "
               "kCampaignPhases. The menu and the manifest disagree.",
               hovered, static_cast<int>(phase));
      return StateAction::KeepCurrent;
    }

    // ChangeToLoading and never ChangeToGameplay, even though the phase is
    // already chosen. On a cold start nothing in kAssets is resident and
    // LoadingState is the only thing that loads it; jumping straight to
    // gameplay gives an invisible player in an empty world. (Game does skip
    // loading for RequestReloadPhase, but only because the assets are already
    // there by then.)
    return StateAction::ChangeToLoading;
  }

  if (IsKeyPressed(KEY_ESCAPE)) {
    return StateAction::RequestQuit;
  }

  return StateAction::KeepCurrent;
}

void MainMenuState::draw() {
  const float screen_w = static_cast<float>(GetScreenWidth());
  const float screen_h = static_cast<float>(GetScreenHeight());

  // Application never clears, so every state owns its own background. This is
  // also what the player sees if the photo failed to load.
  ClearBackground(BLACK);

  if (background.id != 0) {
    const Rectangle src_rec = {0.0f, 0.0f, static_cast<float>(background.width),
                               static_cast<float>(background.height)};
    DrawTexturePro(background, src_rec, bg_dest, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
  }

  // One gradient does all the legibility work: opaque under the buttons, gone
  // before the moon. Both endpoints are RGB-zero so this is a pure alpha ramp
  // with no colour cast over the photo.
  DrawRectangleGradientH(0, 0, static_cast<int>(screen_w * kScrimW),
                         static_cast<int>(screen_h), Fade(BLACK, 0.75f), BLANK);

  const int title_x = static_cast<int>(screen_w * kMarginX);
  const int title_size = static_cast<int>(screen_h * 0.0833f); // 64 px at 768
  const int title_y = static_cast<int>(screen_h * 0.39f) - title_size - 24;
  DrawText("SEKIRO", title_x, title_y, title_size, kInk);
  DrawRectangle(title_x, title_y + title_size + 12,
                static_cast<int>(screen_w * kButtonW * 0.55f), 2,
                Fade(kInk, 0.55f));

  const int label_size = static_cast<int>(screen_h * 0.0339f); // 26 px at 768
  for (size_t i = 0; i < buttons.size(); ++i) {
    const MenuButton &button = buttons[i];
    const bool lit = (static_cast<int>(i) == hovered);

    Color bg_color = Fade(BLACK, 0.35f);
    Color border_color = Fade(kInk, 0.35f);
    float border_thick = 1.0f;
    Color text_color = kInk;

    if (lit) {
      bg_color = Fade(BLACK, 0.55f);
      border_color = GOLD;
      border_thick = 2.0f;
      text_color = GOLD;
    }

    DrawRectangleRec(button.bounds, bg_color);
    DrawRectangleLinesEx(button.bounds, border_thick, border_color);
    if (lit) {
      DrawRectangleRec(Rectangle{button.bounds.x, button.bounds.y, 4.0f,
                                 button.bounds.height},
                       GOLD);
    }

    // Left-aligned rather than centred inside the button
    DrawText(TextFormat("CHAPTER %s", roman(button.phase + 1)),
             static_cast<int>(button.bounds.x) + 20,
             static_cast<int>(button.bounds.y +
                              (button.bounds.height - label_size) * 0.5f),
             label_size, text_color);
  }

  const int hint_size = static_cast<int>(screen_h * 0.0234f); // 18 px at 768
  DrawText("Click a chapter to begin        ESC to quit", title_x,
           static_cast<int>(screen_h * 0.90f), hint_size, Fade(kInk, 0.6f));
}

void MainMenuState::exit() { unloadBackground(); }
