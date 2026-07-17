#pragma once

#include <vector>
#include <Rendering/AssetManager.h>
#include <Rendering/RenderData.h>
#include <Core/CameraController.h>

class GameRenderer {
public:
    GameRenderer();
    ~GameRenderer() = default;

    GameRenderer(const GameRenderer&) = delete;
    GameRenderer& operator=(const GameRenderer&) = delete;

    void initializeAssets();

    void renderGameplay(const CameraController& camera, const std::vector<CharacterRenderData>& entitiesToDraw);

private:
    AssetManager assetManager;

    void drawEnvironment();
};
