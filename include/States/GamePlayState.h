#pragma once

#include <Core/Game.h>
#include <States/GameState.h>
#include <Core/CameraController.h>
#include <Core/InputManager.h>
#include <Entities/Player.h>
#include <Entities/Enemy.h>
#include <Entities/EnemyFactory.h>
#include <GameManager/CombatManager.h>
#include <GameManager/PhysicsManager.h>
#include <Components/Terrain.h>
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
    std::vector<std::unique_ptr<Enemy>> enemies;
    CombatManager combat_manager;
    PhysicsManager physics_manager;
    std::vector<WallObstacle> walls;
    Terrain terrain;

    const InputManager& input_manager;
};