#include <States/MainMenuState.h>
#include <iostream>
#include "raylib.h"

MainMenuState::MainMenuState(){

}

void MainMenuState::Enter() {
    // Load local menu-only graphics or titles here
}

StateAction MainMenuState::Update(float dt) {
    if (IsKeyPressed(KEY_ENTER)) {
        //Dbg purpose
        std::cout << "Enter pressed, requesting transition to Gameplay State" << std::endl;
        return StateAction::ChangeToGameplay;
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        return StateAction::RequestQuit;
    }

    return StateAction::KeepCurrent; 
}

void MainMenuState::Draw() {
    ClearBackground(DARKBLUE);
    
    DrawText("MENU STATE ACTIVE", 100, 200, 24, RAYWHITE);
    DrawText("Press ENTER to request transition to Gameplay State", 100, 260, 16, LIGHTGRAY);
    DrawText("Press ESC to request an absolute Windows application exit", 100, 290, 16, LIGHTGRAY);
}

void MainMenuState::Exit() {
    // Clean up local menu resources here
}