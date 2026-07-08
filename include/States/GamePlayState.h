#pragma once

#include <Core/Game.h>
#include <States/GameState.h>
#include <Core/CameraController.h>
#include <Core/InputManager.h>
#include <memory>

class GamePlayState : public GameState {
public:
    GamePlayState(const InputManager& input_manager);
    ~GamePlayState() override = default;

    void Enter() override;
    StateAction Update(float dt) override;
    void Draw() override;
    void Exit() override;

private:
    std::unique_ptr<CameraController> cameraController;
    Vector3 testPlayerPos;
    const InputManager& input_manager;
};