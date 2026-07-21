#pragma once

#include <Core/CameraController.h>
#include <Rendering/AssetManager.h>
#include <Rendering/IEntityRenderer.h>
#include <Rendering/RenderData.h>
#include <memory>
#include <unordered_map>
#include <vector>

class GameRenderer {
public:
  GameRenderer();
  ~GameRenderer() = default;

  GameRenderer(const GameRenderer &) = delete;
  GameRenderer &operator=(const GameRenderer &) = delete;

  void initializeAssets();

  void renderGameplay(const CameraController &camera,
                      const std::vector<CharacterRenderData> &entitiesToDraw);

private:
  AssetManager assetManager;

  /// Maps each AssetID to the renderer strategy responsible for drawing it.
  std::unordered_map<AssetID, std::unique_ptr<IEntityRenderer>> entityRenderers;

  void drawEnvironment();
};

