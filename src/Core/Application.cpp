#include <Core/Application.h>

Application::Application(){
    // SetConfigFlags(FLAG_WINDOW_TOPMOST | FLAG_WINDOW_UNDECORATED);
    // InitWindow(GetScreenWidth(), GetScreenHeight(), "Borderless fullscreen");
    InitWindow(1366, 768, "Game");
    SetTargetFPS(60);

    game.pushState(std::make_unique<MainMenuState>());
}

Application::~Application(){
    CloseAudioDevice();
    CloseWindow();
}

void Application::run(){
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