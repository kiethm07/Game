#pragma once

#include <States/GameState.h>
#include <cstddef>
#include <raylib.h>
#include <vector>

/// Forward-declared rather than included. Only a reference is stored, so the
/// full type belongs in the .cpp -- and this header used to `#include
/// <Core/Game.h>`, which Game.h includes right back. That cycle survived only
/// because nothing here needed Game; it does not survive this class growing
/// members, so it is gone.
class Campaign;

/// The title screen: a cover photo and one button per campaign phase.
///
/// Holds a Campaign& because picking a chapter *is* moving the campaign cursor.
/// StateAction is deliberately payload-free (see StateAction.h), so the choice
/// cannot ride on the returned enum -- it is written into the service Game owns
/// and every state downstream reads it live.
class MainMenuState : public GameState {
public:
  explicit MainMenuState(Campaign &campaign);
  ~MainMenuState() override;

  // Holds a raw GPU handle, so copying it would double-free the texture. Same
  // reason AssetManager deletes these.
  MainMenuState(const MainMenuState &) = delete;
  MainMenuState &operator=(const MainMenuState &) = delete;

  void enter() override;
  StateAction update(float dt) override;
  void draw() override;
  void exit() override;

private:
  /// One clickable row: where it is, and which phase it starts.
  ///
  /// Nested rather than pulled out into a UI header because there is exactly
  /// one screen in this game with buttons on it. When a second one exists this
  /// moves out; inventing include/UI/Button.h now would fix an API before
  /// anything can disagree with it.
  ///
  /// Carries the phase as an *index*, never as the `const char *` name out of
  /// kCampaignPhases. Identity goes through the index so nothing here can ever
  /// depend on two pointers into that table comparing equal.
  struct MenuButton {
    Rectangle bounds;
    size_t phase;
  };

  /// Lay the stack out from campaign.count(). Called in enter() rather than the
  /// constructor because it needs GetScreenWidth().
  void buildButtons();

  /// Decode the cover photo, downscaled to the size it will actually be drawn
  /// at. Leaves `background.id` at 0 and logs if the file is missing, which
  /// draw() treats as "flat background" rather than as a reason to crash.
  void loadBackground();

  /// Free the texture and zero the handle. Called from both exit() and the
  /// destructor; the zeroing is what makes the second call a no-op.
  void unloadBackground();

  Campaign &campaign;

  Texture2D background{};

  /// The centred crop of `background` that fills the window. Computed once in
  /// enter() -- it cannot change while the state is alive.
  Rectangle bg_source{};

  std::vector<MenuButton> buttons;

  /// Index into `buttons`, or -1 for none.
  int hovered = -1;
};
