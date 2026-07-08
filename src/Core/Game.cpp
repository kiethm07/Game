#include <Core/Game.h>

Game::Game(){}

Game::~Game() {
    while (!states.empty()) {
        PopState();
    }
}

void Game::Update(float dt) {
    if (!states.empty()) {
        StateAction action = states.back()->Update(dt);
        // // Handle state actions if needed
        if (action == StateAction::RequestQuit) {
            // Handle quit request, e.g., exit the game loop
        }
        if (action == StateAction::ChangeToMenu) {
            // ChangeState(std::make_unique<MainMenuState>());
            PopState();
            PushState(std::make_unique<MainMenuState>());
        }
        if (action == StateAction::ChangeToGameplay) {
            PopState();
            PushState(std::make_unique<GamePlayState>());
            // ChangeState(std::make_unique<GamePlayState>());
        }
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