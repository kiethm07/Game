#include <Core/Game.h>
#include <States/LoadingState.h>
#include <States/EndGameState.h>

Game::Game()
    : sound_controller(asset_manager), attack_registry(AttackRegistry::instance()) {}

Game::~Game() {
    while (!states.empty()) {
        popState();
    }
}

void Game::start() { pushMainMenu(); }

void Game::pushMainMenu() {
    pushState(std::make_unique<MainMenuState>(sound_controller, asset_manager, campaign));
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
            // A flag rather than an exit() or a break: Application owns the
            // loop, and unwinding it normally is what runs ~Game -> popState ->
            // every state's exit(), freeing GPU handles before CloseWindow().
            quit_requested = true;
        }
        if (action == StateAction::ChangeToMenu) {
            popState();
            // Leaving a phase for the menu abandons the run: the cursor goes
            // back to the first phase and the carry is dropped. After
            // popState(), so that a carry written on the way out cannot
            // survive the reset.
            campaign.restart();
            pushMainMenu();
        }
        if (action == StateAction::ChangeToLoading) {
            popState();
            pushState(std::make_unique<LoadingState>(asset_manager, campaign));
        }
        if (action == StateAction::ChangeToGameplay) {
            popState();
            pushState(std::make_unique<GameplayState>(input_manager, asset_manager, sound_controller, campaign));
        }
        if (action == StateAction::RequestReloadPhase) {
            // RequestNextPhase's shape, minus the cursor move: this rebuilds
            // the phase already running, so LevelLoader::load re-reads
            // level.json and its enemies.json overlay.
            //
            // Straight to GameplayState rather than through LoadingState. The
            // assets live in `asset_manager`, which outlives every state, so a
            // loading pass would walk kAssets top to bottom to load nothing.
            // What this actually costs is one level load, one collision mesh,
            // one GameRenderer and one navmesh bake -- strictly less than the
            // phase transition above, which does all of that plus the loading
            // screen. That is what makes it an authoring key.
            //
            // No carry snapshot. This is not a seam, it is the same phase
            // again, and the fresh Player comes up at full health -- which is
            // what you want when the thing you are iterating on is the fight.
            popState();
            pushState(std::make_unique<GameplayState>(
                input_manager, asset_manager, sound_controller, campaign));
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
                // That was the last phase. The run is over -> Transition to End Game Scene!
                popState();
                campaign.restart();
                pushState(std::make_unique<EndGameState>(sound_controller, asset_manager, campaign));
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