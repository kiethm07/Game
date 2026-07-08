#pragma once

#include <Core/Game.h>
#include <States/GameState.h>
#include <Core/CameraController.h>
#include <Core/InputManager.h>


class GamePlayState : public GameState {
public:
    GamePlayState();
    ~GamePlayState() override = default;

    void Enter() override;
    StateAction Update(float dt) override;
    void Draw() override;
    void Exit() override;

private:
    std::unique_ptr<CameraController> cameraController;
    Vector3 testPlayerPos;
    //InputManager &inputManager;
};