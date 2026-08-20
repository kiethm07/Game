#include <Core/Game.h>
#include <States/LoadingState.h>

Game::Game()
    : sound_controller(asset_manager), attack_registry(AttackRegistry::instance()) {}

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
            popState();
            // Leaving a phase for the menu abandons the run: the cursor goes
            // back to the first phase and the carry is dropped. After
            // popState(), so that a carry written on the way out cannot
            // survive the reset.
            campaign.restart();
            pushState(std::make_unique<MainMenuState>());
        }
        if (action == StateAction::ChangeToLoading) {
            popState();
            pushState(std::make_unique<LoadingState>(asset_manager, campaign));
        }
        if (action == StateAction::ChangeToGameplay) {
            popState();
            pushState(std::make_unique<GameplayState>(input_manager, asset_manager, sound_controller, campaign));
        }
        if (action == StateAction::RequestNextPhase) {
            // The destination is not in the action -- it is in `campaign`.
            // That is what keeps StateAction payload-free, and keeping it
            // payload-free is what lets a state request its own replacement
            // without a Game back-pointer to route the request through.
            //
            // advance() both moves the cursor and answers whether there was
            // anywhere to move to, so the run ending is the same branch as the
            // run continuing rather than a separate query that could disagree.
            if (campaign.advance()) {
                popState();
                pushState(std::make_unique<LoadingState>(asset_manager, campaign));
            } else {
                // That was the last phase. The run is over.
                popState();
                campaign.restart();
                pushState(std::make_unique<MainMenuState>());
            }
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