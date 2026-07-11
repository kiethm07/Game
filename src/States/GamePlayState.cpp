#include <States/GameplayState.h>
#include <Core/Game.h>
#include <iostream>
#include <cmath> 
#include "raylib.h"

GameplayState::GameplayState(const InputManager& input_manager):
    input_manager(input_manager)
{
    camera_controller = std::make_unique<CameraController>();
    player = std::make_unique<Player>(input_manager);
}

void GameplayState::enter() {
    // Load local menu-only graphics or titles here
}

StateAction GameplayState::update(float dt) {
    // 1. Tick the player. The player internally reads input and shifts its own position safely.
    player->update(dt, camera_controller->getCameraForward(), camera_controller->getCameraRight());

    // 2. Safely spy on the player's new position via the const getter
    Vector3 current_player_pos = player->getPosition();

    // 3. Update the camera tracking matrix using that position
    Vector2 mouse_delta = input_manager.getRawMouseDelta();
    camera_controller->update(current_player_pos, mouse_delta);

    // State transition handling
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
        return StateAction::ChangeToMenu;
    }
    
    return StateAction::KeepCurrent; 
}

void GameplayState::draw() {
    ClearBackground(RAYWHITE);
    
    // Establish 3D Projection space
    BeginMode3D(camera_controller->getCamera());

        // 1. Draw static environment layout
        DrawGrid(300, 10.0f); 

        // 2. Command the player entity to draw itself!
        // No hardcoded DrawCube offsets here anymore.
        player->draw(); 

    EndMode3D();

    // --- 2D UI LAYER ---
    DrawFPS(10, 10);
    DrawText("Phase 1.5: Architecture Integrated. Player entity encapsulates movement logic.", 10, 40, 20, DARKGRAY);
}

void GameplayState::exit() {
    // Clean up local menu resources here
}