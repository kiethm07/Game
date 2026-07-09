#pragma once
#include <Core/InputManager.h>
#include <States/GameState.h>
#include <States/MainMenuState.h>
#include <States/GameplayState.h>
#include <memory>
#include <vector>

class Game{
public:
    Game();
    ~Game();

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    void update(float dt);
    void draw();

    void pushState(std::unique_ptr<GameState> state);
    void popState();
    void changeState(std::unique_ptr<GameState> state);

    bool isEmpty() const { return states.empty(); }

    // InputManager& GetInputManager() { return inputManager; }

private:
    InputManager input_manager;
    std::vector<std::unique_ptr<GameState>> states;
};