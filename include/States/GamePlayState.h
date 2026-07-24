#pragma once

#include <Core/Game.h>
#include <States/GameState.h>
#include <Core/CameraController.h>
#include <Core/InputManager.h>
#include <Entities/Player.h>
#include <Entities/Enemy.h>
#include <Rendering/GameRenderer.h>
#include <Rendering/RenderData.h>
#include <memory>
#include <vector>

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
    std::unique_ptr<Enemy> enemy;
    std::unique_ptr<GameRenderer> renderer;

    /// Reused across frames to avoid a per-frame heap allocation.
    std::vector<CharacterRenderData> renderList;

    const InputManager& input_manager;
};