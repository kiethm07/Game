#include <States/MainMenuState.h>
#include <iostream>
#include "raylib.h"

MainMenuState::MainMenuState(){

}

void MainMenuState::enter() {
    // Load local menu-only graphics or titles here
}

StateAction MainMenuState::update(float dt) {
    if (IsKeyPressed(KEY_ENTER)) {
        return StateAction::ChangeToLoading;
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        return StateAction::RequestQuit;
    }

    return StateAction::KeepCurrent; 
}

void MainMenuState::draw() {
    ClearBackground(DARKBLUE);
    
    DrawText("Main Menu", 100, 200, 24, RAYWHITE);
    DrawText("Press ENTER to request transition to Gameplay State", 100, 260, 16, RAYWHITE);
    DrawText("Press ESC to request an absolute Windows application exit", 100, 290, 16, RAYWHITE);
}

void MainMenuState::exit() {
    // Clean up local menu resources here
}