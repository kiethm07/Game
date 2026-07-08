#pragma once
#include <Core/InputManager.h>
#include <States/GameState.h>
#include <States/MainMenuState.h>
#include <States/GamePlayState.h>
#include <memory>
#include <vector>

class Game{
public:
    Game();
    ~Game();

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    void Update(float dt);
    void Draw();

    void PushState(std::unique_ptr<GameState> state);
    void PopState();
    void ChangeState(std::unique_ptr<GameState> state);

    bool IsEmpty() const { return states.empty(); }

private:
    InputManager inputManager;
    std::vector<std::unique_ptr<GameState>> states;
};