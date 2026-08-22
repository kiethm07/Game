#include <Core/AssetPaths.h>
#include <States/LoadingState.h>
#include <Rendering/AssetManifest.h>
#include <raylib.h>

LoadingState::LoadingState(AssetManager& asset_manager, const Campaign& campaign)
    : asset_manager(asset_manager), campaign(campaign) {}

void LoadingState::enter() {
    // Nothing here is clickable, and without this the OS cursor is left over
    // from the menu, floating on top of the progress bar for a second on every
    // load. Every state declares its own mode; none inherits one.
    DisableCursor();
}

StateAction LoadingState::update(float dt) {
    size_t num_assets = sizeof(kAssets) / sizeof(kAssets[0]);

    // Smoothly animate the progress bar
    if (display_progress < target_progress) {
        display_progress += dt * 0.8f; // Move at a fixed speed
        if (display_progress > target_progress) {
            display_progress = target_progress;
        }
    }

    switch (phase) {
        case LoadPhase::Init:
            target_progress = 0.1f;
            phase = LoadPhase::Models;
            break;
            
        case LoadPhase::Models:
            if (current_asset_index < num_assets) {
                const auto& entry = kAssets[current_asset_index];
                if (entry.modelPath) {
                    asset_manager.loadModel(entry.id, assets::path(entry.modelPath));
                }
                current_asset_index++;
                target_progress = 0.1f + 0.2f * ((float)current_asset_index / num_assets);
            } else {
                phase = LoadPhase::StartAsyncAnims;
                current_asset_index = 0;
            }
            break;
            
        case LoadPhase::StartAsyncAnims:
            // We launch animation loading in a background thread so the main thread
            // can continue pumping update/draw to animate the progress bar!
            async_anim_task = std::async(std::launch::async, [this, num_assets]() {
                for (size_t i = 0; i < num_assets; i++) {
                    const auto& entry = kAssets[i];
                    if (entry.animPath) {
                        asset_manager.loadAnimations(entry.id, assets::path(entry.animPath));
                    }
                }
            });
            phase = LoadPhase::WaitAsyncAnims;
            target_progress = 0.9f; // It will slowly fill up to 90% while waiting
            break;
            
        case LoadPhase::WaitAsyncAnims:
            // Check if the background thread has finished without blocking
            if (async_anim_task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                phase = LoadPhase::Aliases;
            }
            break;
            
        case LoadPhase::Aliases:
            for (size_t i = 0; i < num_assets; i++) {
                const auto& entry = kAssets[i];
                if (entry.sharedModelId) {
                    asset_manager.shareModel(entry.id, *entry.sharedModelId);
                }
                if (entry.sharedAnimId) {
                    asset_manager.shareAnimations(entry.id, *entry.sharedAnimId);
                }
            }
            target_progress = 1.0f;
            phase = LoadPhase::Done;
            break;
            
        case LoadPhase::Done:
            if (display_progress >= 0.99f) {
                return StateAction::ChangeToGameplay;
            }
            break;
    }

    return StateAction::KeepCurrent;
}

void LoadingState::draw() {
    ClearBackground(BLACK);
    
    int screen_width = GetScreenWidth();
    int screen_height = GetScreenHeight();
    
    const char* text = "Loading Assets...";
    int font_size = 40;
    int text_width = MeasureText(text, font_size);
    DrawText(text, (screen_width - text_width) / 2, screen_height / 2 - 50, font_size, RAYWHITE);
    
    // Draw Progress bar
    int bar_width = 400;
    int bar_height = 20;
    int bar_x = (screen_width - bar_width) / 2;
    int bar_y = screen_height / 2 + 20;
    
    DrawRectangle(bar_x, bar_y, bar_width, bar_height, DARKGRAY);
    DrawRectangle(bar_x, bar_y, (int)(bar_width * display_progress), bar_height, GREEN);
    DrawRectangleLines(bar_x, bar_y, bar_width, bar_height, LIGHTGRAY);

    // Which phase is coming. This screen is now reached twice for different
    // reasons -- starting a run, and crossing a phase seam -- and without a
    // name the two are indistinguishable, as are the three phases from each
    // other.
    const char* phase_text = TextFormat("%s  —  Phase %d of %d",
                                        campaign.currentName(),
                                        (int)campaign.index() + 1,
                                        (int)campaign.count());
    int phase_width = MeasureText(phase_text, 20);
    DrawText(phase_text, (screen_width - phase_width) / 2, bar_y + bar_height + 16,
             20, GRAY);
}

void LoadingState::exit() {}
