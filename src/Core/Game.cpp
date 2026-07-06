#include <Core/Game.h>

Game::Game(){}

Game::~Game() {
    while (!states.empty()) {
        PopState();
    }
}

void Game::Update(float dt) {
    if (!states.empty()) {
        states.back()->Update(dt);
    }
}

void Game::Draw() {
    if (!states.empty()) {
        states.back()->Draw();
    }
}

void Game::PushState(std::unique_ptr<GameState> state) {
    states.push_back(std::move(state));
    states.back()->Enter();
}

void Game::PopState() {
    if (!states.empty()) {
        states.back()->Exit();
        states.pop_back();
    }
}

void Game::ChangeState(std::unique_ptr<GameState> state) {
    PopState();
    PushState(std::move(state));
}