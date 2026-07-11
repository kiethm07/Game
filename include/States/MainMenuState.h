#pragma once

#include <Core/Game.h>
#include <States/GameState.h>

class MainMenuState : public GameState {
public:
    MainMenuState();
    ~MainMenuState() override = default;

    void enter() override;
    StateAction update(float dt) override;
    void draw() override;
    void exit() override;
};