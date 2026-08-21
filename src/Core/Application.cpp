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

  game.pushState(std::make_unique<MainMenuState>());
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
  DisableCursor();
  while (!WindowShouldClose()) {
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