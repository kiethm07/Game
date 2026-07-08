#include <States/GamePlayState.h>
#include <iostream>
#include <cmath> 
#include "raylib.h"
#include "Core/Game.h"

GamePlayState::GamePlayState(const InputManager& input_manager):
    input_manager(input_manager)
{
    cameraController = std::make_unique<CameraController>();
    testPlayerPos = { 0.0f, 0.5f, 0.0f };
}

void GamePlayState::Enter() {
    // Load local menu-only graphics or titles here
}

StateAction GamePlayState::Update(float dt) {
    // Access your central dumb InputManager instance
    const InputManager& input = input_manager;

    // 1. Update the camera using the raw mouse delta from our manager
    Vector2 mouseDelta = input.GetRawMouseDelta();
    cameraController->Update(testPlayerPos, mouseDelta);

    // 2. Resolve Directional Intent (The "Brain" layer)
    Vector3 moveDir = { 0.0f, 0.0f, 0.0f };

    if (input.IsActionHeld(GameAction::MOVE_FORWARD))  moveDir.z -= 1.0f; // -Z is forward in Raylib 3D
    if (input.IsActionHeld(GameAction::MOVE_BACKWARD)) moveDir.z += 1.0f; // +Z is backward
    if (input.IsActionHeld(GameAction::MOVE_LEFT))     moveDir.x -= 1.0f; // -X is left
    if (input.IsActionHeld(GameAction::MOVE_RIGHT))    moveDir.x += 1.0f; // +X is right

    // 3. Normalize the vector right here so diagonal running isn't faster
    float length = std::sqrt((moveDir.x * moveDir.x) + (moveDir.z * moveDir.z));
    if (length > 0.0f) {
        moveDir.x /= length;
        moveDir.z /= length;
    }

    // 4. Apply movement physics to our placeholder cube coordinates
    const float speed = 5.0f;
    testPlayerPos.x += moveDir.x * speed * dt;
    testPlayerPos.z += moveDir.z * speed * dt;

    // State transition handling
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
        return StateAction::ChangeToMenu;
    }
    
    return StateAction::KeepCurrent; 
}

void GamePlayState::Draw() {
    ClearBackground(RAYWHITE);
    BeginMode3D(cameraController->GetCamera());

        // 1. Draw the floor (Visual Map)
        DrawGrid(300, 10.0f); 

        // 2. Draw the player (Grey-boxing) - Notice NO physics logic here anymore!
        DrawCube(testPlayerPos, 1.0f, 1.0f, 1.0f, BLUE);
        
        // Draws an outline around the cube to make it look 3D against the background
        DrawCubeWires(testPlayerPos, 1.0f, 1.0f, 1.0f, BLACK); 

    EndMode3D();

    // --- 2D UI LAYER ---
    DrawFPS(10, 10);
    DrawText("Phase 1: 3D Sandbox. Use Mouse to look around. WASD to move.", 10, 40, 20, DARKGRAY);
}

void GamePlayState::Exit() {
    // Clean up local menu resources here
}