#include <raylib.h>
#include <iostream>
#include <filesystem>
#include <fstream>

int main() {
    InitWindow(800, 600, "Raylib window with FetchContent");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawText("Congrats! You created your first window!", 190, 200, 20, LIGHTGRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}