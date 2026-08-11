#include <Core/Game.h>
#include <States/LoadingState.h>

Game::Game()
    : attack_registry(AttackRegistry::instance()) {}

Game::~Game() {
    while (!states.empty()) {
        popState();
    }
}

void Game::update() {
    if (!states.empty()) {
        time_manager.update();
        input_manager.update();
        sound_controller.updateMusic();

        float dt = time_manager.getDeltaTime();
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
        if (action == StateAction::ChangeToLoading) {
            popState();
            pushState(std::make_unique<LoadingState>(asset_manager));
        }
        if (action == StateAction::ChangeToGameplay) {
            popState();
            pushState(std::make_unique<GameplayState>(input_manager, asset_manager, sound_controller));
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