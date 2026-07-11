#include <Core/Game.h>

Game::Game(){}

Game::~Game() {
    while (!states.empty()) {
        popState();
    }
}

void Game::update(float dt) {
    if (!states.empty()) {
        input_manager.update();
        StateAction action = states.back()->update(dt);
        // // Handle state actions if needed
        if (action == StateAction::RequestQuit) {
            // Handle quit request, e.g., exit the game loop
        }
        if (action == StateAction::ChangeToMenu) {
            // ChangeState(std::make_unique<MainMenuState>());
            popState();
            pushState(std::make_unique<MainMenuState>());
        }
        if (action == StateAction::ChangeToGameplay) {
            popState();
            pushState(std::make_unique<GameplayState>(input_manager));
            // ChangeState(std::make_unique<GamePlayState>());
        }
    }
}

void Game::draw() {
    if (!states.empty()) {
        states.back()->draw();
    }
}

void Game::pushState(std::unique_ptr<GameState> state) {
    states.push_back(std::move(state));
    states.back()->enter();
}

void Game::popState() {
    if (!states.empty()) {
        states.back()->exit();
        states.pop_back();
    }
}

void Game::changeState(std::unique_ptr<GameState> state) {
    popState();
    pushState(std::move(state));
}