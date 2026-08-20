#pragma once
#include <States/GameState.h>
#include <Level/Campaign.h>
#include <Rendering/AssetManager.h>
#include <string>
#include <future>
#include <vector>

class LoadingState : public GameState {
public:
    LoadingState(AssetManager& asset_manager, const Campaign& campaign);
    ~LoadingState() override = default;

    void enter() override;
    StateAction update(float dt) override;
    void draw() override;
    void exit() override;

private:
    AssetManager& asset_manager;

    /// Read only, and only to say which phase is coming. Without it every
    /// phase change shows an identical anonymous bar, and a phase whose
    /// level.json failed to load is invisible until the empty world appears.
    const Campaign& campaign;
    size_t current_asset_index = 0;
    
    enum class LoadPhase {
        Init,
        Models,
        StartAsyncAnims,
        WaitAsyncAnims,
        Aliases,
        Done
    };
    
    LoadPhase phase = LoadPhase::Init;
    std::future<void> async_anim_task;
    
    float display_progress = 0.0f;
    float target_progress = 0.0f;
};
