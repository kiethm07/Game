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

  /// 3D world pass. Assumes the caller has already opened a 3D scope
  /// (BeginMode3D) and will close it; draws environment + entities into it.
  void renderGameplay(const CameraController &camera,
                      const std::vector<CharacterRenderData> &entitiesToDraw);

  /// 2D overlay pass: HUD/debug text. Must be called after EndMode3D.
  void drawUI();

private:
  AssetManager assetManager;

  /// Maps each AssetID to the renderer strategy responsible for drawing it.
  std::unordered_map<AssetID, std::unique_ptr<IEntityRenderer>> entityRenderers;

  /// 3D world pass: environment + every entity's skinned/proxy geometry.
  void drawWorld(const CameraController &camera,
                 const std::vector<CharacterRenderData> &entitiesToDraw);
  void drawEnvironment();
};

