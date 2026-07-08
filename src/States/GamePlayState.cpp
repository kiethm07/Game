#include <States/GamePlayState.h>
#include <iostream>
#include "raylib.h"

GamePlayState::GamePlayState(){
    cameraController = std::make_unique<CameraController>();
    testPlayerPos = { 0.0f, 0.5f, 0.0f };
}

void GamePlayState::Enter() {
    // Load local menu-only graphics or titles here
}

StateAction GamePlayState::Update(float dt) {
    // Update the camera based on the player's position and mouse movement
    Vector2 mouseDelta = GetMouseDelta();
    cameraController->Update(testPlayerPos, mouseDelta);

    if (IsKeyPressed(KEY_ENTER)) {
        return StateAction::ChangeToMenu;
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        return StateAction::ChangeToMenu;
    }
    return StateAction::KeepCurrent; 
}

void GamePlayState::Draw() {
    ClearBackground(RAYWHITE);
    BeginMode3D(cameraController->GetCamera());

        // 1. Draw the floor (Visual Map)
        // Creates a 20x20 grid with 1.0f spacing. 
        DrawGrid(300, 10.0f); 

        // update player position based on input (for testing purposes)
        // testPlayerPos.x += GetGame().GetInputManager().GetMovementVector().x * 5.0f * GetFrameTime();
        // testPlayerPos.z += GetGame().GetInputManager().GetMovementVector().y * 5.0f * GetFrameTime();
        // 2. Draw the player (Grey-boxing)
        DrawCube(testPlayerPos, 1.0f, 1.0f, 1.0f, BLUE);
        
        // Draws an outline around the cube to make it look 3D against the background
        DrawCubeWires(testPlayerPos, 1.0f, 1.0f, 1.0f, BLACK); 

    EndMode3D();
    // --------------------------

    // --- 2D UI LAYER ---
    DrawFPS(10, 10);
    DrawText("Phase 1: 3D Sandbox. Use Mouse to look around.", 10, 40, 20, DARKGRAY);
}

void GamePlayState::Exit() {
    // Clean up local menu resources here
}