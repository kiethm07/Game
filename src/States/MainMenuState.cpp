#include <States/MainMenuState.h>

#include <Core/AssetPaths.h>
#include <Level/Campaign.h>
#include <cmath>
#include <raylib.h>

namespace {

/// The cover photo. One file, read once per menu entry, owned by the state.
constexpr const char *kBackgroundAsset = "mainmenuphoto.jpg";

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

MainMenuState::MainMenuState(Campaign &campaign) : campaign(campaign) {}

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
}

void MainMenuState::loadBackground() {
  Image image = LoadImage(assets::path(kBackgroundAsset).c_str());

  if (IsImageValid(image)) {
    const int screen_w = GetScreenWidth();
    const int screen_h = GetScreenHeight();

    // fmaxf covers and crops; fminf would contain and letterbox. The photo is
    // 3239x1851 (1.7499) against a 1366x768 window (1.7786), so this is
    // width-driven and the crop comes off the top and bottom.
    //
    // Resized on the CPU before upload because the file decodes to a 3-channel
    // image -- raylib maps comp==3 to R8G8B8 -- and 3239x1851x3 is 17.1 MiB of
    // VRAM to draw a 1366x768 rectangle. The resize takes it to 1366x781, or
    // 3.05 MiB, and lands texel-to-pixel 1:1 so no filter is wanted and
    // mipmaps would be 33% more VRAM for a texture that is never minified.
    const float scale = fmaxf(static_cast<float>(screen_w) / image.width,
                              static_cast<float>(screen_h) / image.height);
    ImageResize(&image, static_cast<int>(ceilf(image.width * scale)),
                static_cast<int>(ceilf(image.height * scale)));

    background = LoadTextureFromImage(image);
    bg_source = coverSource(background.width, background.height, screen_w,
                            screen_h);
  }

  // Outside the branch on purpose: raylib null-checks the data pointer, so this
  // is safe on an image that failed to load, and putting it inside would leak
  // the 17 MiB decode buffer on every menu entry.
  UnloadImage(image);

  if (background.id == 0) {
    TraceLog(LOG_WARNING,
             "MainMenuState: could not load '%s'; the menu will come up on a "
             "flat background. The chapter buttons still work.",
             kBackgroundAsset);
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
    DrawTexturePro(background, bg_source,
                   Rectangle{0.0f, 0.0f, screen_w, screen_h},
                   Vector2{0.0f, 0.0f}, 0.0f, WHITE);
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

    DrawRectangleRec(button.bounds, Fade(BLACK, lit ? 0.55f : 0.35f));
    DrawRectangleLinesEx(button.bounds, lit ? 2.0f : 1.0f,
                         lit ? GOLD : Fade(kInk, 0.35f));
    if (lit) {
      DrawRectangleRec(Rectangle{button.bounds.x, button.bounds.y, 4.0f,
                                 button.bounds.height},
                       GOLD);
    }

    // Left-aligned rather than centred inside the button: centring would be a
    // seventh copy of the MeasureText idiom this codebase already repeats six
    // times, and a ragged left edge is worse here than a ragged right one.
    DrawText(TextFormat("CHAPTER %s", roman(button.phase + 1)),
             static_cast<int>(button.bounds.x) + 20,
             static_cast<int>(button.bounds.y +
                              (button.bounds.height - label_size) * 0.5f),
             label_size, lit ? GOLD : kInk);
  }

  const int hint_size = static_cast<int>(screen_h * 0.0234f); // 18 px at 768
  DrawText("Click a chapter to begin        ESC to quit", title_x,
           static_cast<int>(screen_h * 0.90f), hint_size, Fade(kInk, 0.6f));
}

void MainMenuState::exit() { unloadBackground(); }
