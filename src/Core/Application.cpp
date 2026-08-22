#include <Core/AssetPaths.h>
#include <Core/Application.h>

Application::Application() {
  // SetConfigFlags(FLAG_WINDOW_TOPMOST | FLAG_WINDOW_UNDECORATED);
  // InitWindow(GetScreenWidth(), GetScreenHeight(), "Borderless fullscreen");
  InitWindow(1366, 768, "Game");

  // Before anything loads. Resolving here rather than lazily is what turns
  // "no asset root" into one message at startup naming every directory that
  // was tried, instead of a window that opens and then fails asset by asset --
  // no models, no shaders, the fallback ground plane, and nothing saying why.
  //
  // GetApplicationDirectory needs the platform layer up, so this cannot move
  // above InitWindow.
  assets::resolve();

  InitAudioDevice();
  SetTargetFPS(60);

  // ESC stops being a window-close key and becomes ordinary input.
  //
  // It was raylib's default exit key, and that quietly broke the one thing it
  // looked like it was doing: GameplayState returns ChangeToMenu on ESC, Game
  // duly built a fresh menu, and then WindowShouldClose() ended the loop on the
  // same iteration -- so ESC never actually returned anyone to the menu. Now
  // the menu's ESC goes through StateAction::RequestQuit instead, which the
  // loop below reads. The two changes only work together: this line alone would
  // leave no way out at all.
  SetExitKey(KEY_NULL);

  // After InitWindow and assets::resolve(), because the menu loads a texture in
  // enter(). See Game::start().
  game.start();
}

Application::~Application() {
  if (IsAudioDeviceReady()) {
    CloseAudioDevice();
  }
  if (IsWindowReady()) {
    CloseWindow();
  }
}

void Application::run() {
  // No DisableCursor() here any more, and its absence is load-bearing.
  //
  // It used to run at this point -- which is *after* the constructor pushed the
  // first state and ran its enter(). A menu that enables the cursor in enter()
  // would have had it switched straight back off, and the mouse-driven title
  // screen would have come up dead for no visible reason. Each state now
  // declares its own cursor mode in enter(); since every transition in
  // Game::update is pop-then-push, the incoming state's enter() always wins.
  while (!WindowShouldClose() && !game.shouldQuit()) {
    // 1. Process Update vectors and evaluate state transitions
    game.update();
    // 2. Clear buffers and execute paint matrices
    BeginDrawing();
    game.draw();
    EndDrawing();
  }
  // Window is closed by ~Application() after all game objects (including
  // AssetManager) are fully destroyed. Do NOT call CloseWindow() here.
}