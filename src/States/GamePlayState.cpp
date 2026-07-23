#include <States/GameplayState.h>
#include <Core/Game.h>
#include <iostream>
#include <cmath> 
#include <cassert>
#include "raylib.h"

GameplayState::GameplayState(const InputManager& input_manager) :
    input_manager(input_manager)
{
    camera_controller = std::make_unique<CameraController>();
    player = std::make_unique<Player>(input_manager);

    // Spawn test enemies via factory
    enemies.push_back(EnemyFactory::createEnemy(EnemyType::Swordman, { 0.0f, 0.0f, 5.0f }));
    enemies.push_back(EnemyFactory::createEnemy(EnemyType::Swordman, { 3.0f, 0.0f, 8.0f }));
}

void GameplayState::enter() {
    // Load local menu-only graphics or titles here
}

StateAction GameplayState::update(float dt) {
    // 1. Tick Entities
    player->update(dt, camera_controller->getCameraForward(), camera_controller->getCameraRight());

    Vector3 player_pos = player->getPosition();
    for (auto& enemy : enemies) {
        enemy->update(dt, player_pos);
    }

    // 2. Gather Invariant Characters
    std::vector<Character*> active_characters;
    active_characters.reserve(1 + enemies.size());

    active_characters.push_back(player.get());
    for (auto& enemy : enemies) {
        active_characters.push_back(enemy.get());
    }

    // 3. Resolve Combat
    combat_manager.update(active_characters);

    // 4. Update Camera & Transitions
    camera_controller->update(player_pos, input_manager.getRawMouseDelta());

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

    // 2. Draw Entities
    player->draw();
    for (const auto& enemy : enemies) {
        enemy->draw();
    }

    // Gather active characters for debug drawing
    std::vector<Character*> active_characters;
    active_characters.reserve(1 + enemies.size());
    active_characters.push_back(player.get());
    for (const auto& enemy : enemies) {
        active_characters.push_back(enemy.get());
    }

    // --- DRAW HITBOX & HURTBOX WIREFRAMES ---
    combat_manager.drawDebug(active_characters);

    EndMode3D();

    // --- 2D UI LAYER ---
    DrawFPS(10, 10);
    DrawText("Phase 1.5: Architecture Integrated. Player entity encapsulates movement logic.", 10, 40, 20, DARKGRAY);

    // --- HEALTH BARS ---
    player->drawHPBar2D();
    for (const auto& enemy : enemies) {
        enemy->drawHPBar(camera_controller->getCamera());
    }
}

void GameplayState::exit() {
    // Clean up local menu resources here
}