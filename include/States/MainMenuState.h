#pragma once

#include <Core/Game.h>
#include <States/GameState.h>

class MainMenuState : public GameState {
public:
    MainMenuState();
    ~MainMenuState() override = default;

    void Enter() override;
    StateAction Update(float dt) override;
    void Draw() override;
    void Exit() override;
};