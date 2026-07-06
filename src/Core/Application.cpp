#include <Core/Application.h>

Application::Application(){
    // SetConfigFlags(FLAG_WINDOW_TOPMOST | FLAG_WINDOW_UNDECORATED);
    // InitWindow(GetScreenWidth(), GetScreenHeight(), "Borderless fullscreen");
    InitWindow(1366, 768, "Game");
    SetTargetFPS(60);

    game.PushState(std::make_unique<MainMenuState>());
}

Application::~Application(){
    CloseAudioDevice();
    CloseWindow();
}

void Application::run(){
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        // 1. Process Update vectors and evaluate state transitions
        game.Update(dt);
        // 2. Clear buffers and execute paint matrices
        BeginDrawing();
        game.Draw();
        EndDrawing();
    }
    CloseWindow();
}