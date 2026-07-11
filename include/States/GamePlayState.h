#pragma once

#include <Core/Game.h>
#include <States/GameState.h>
#include <Core/CameraController.h>
#include <Core/InputManager.h>
#include <Entities/Player.h>
#include <memory>

class GameplayState : public GameState {
public:
    GameplayState(const InputManager& input_manager);
    ~GameplayState() override = default;

    void enter() override;
    StateAction update(float dt) override;
    void draw() override;
    void exit() override;

private:
    std::unique_ptr<CameraController> camera_controller;
    std::unique_ptr<Player> player;
    
    const InputManager& input_manager;
};