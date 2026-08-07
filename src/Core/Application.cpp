#include <Core/Application.h>

Application::Application() {
  // SetConfigFlags(FLAG_WINDOW_TOPMOST | FLAG_WINDOW_UNDECORATED);
  // InitWindow(GetScreenWidth(), GetScreenHeight(), "Borderless fullscreen");
  InitWindow(1366, 768, "Game");
  SetTargetFPS(getenv("BENCH") ? 0 : 60); // TEMP-BENCH: uncap to see real cost

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