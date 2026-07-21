#include <Rendering/DebugCubeRenderer.h>
#include <Rendering/GameRenderer.h>
#include <Rendering/SkinnedEntityRenderer.h>

// ---------------------------------------------------------------------------
// Asset Manifest
// ---------------------------------------------------------------------------
// To add a new entity:
//   1. Add its AssetID to AssetID.h
//   2. Add one row here (modelPath may be nullptr for non-modelled entities)
//   3. Create and register its IEntityRenderer in initializeAssets() below
// ---------------------------------------------------------------------------
namespace {
struct AssetEntry {
  AssetID id;
  const char *modelPath;             ///< nullptr → no model to load
  const char *animPath;              ///< nullptr → no animations to load
  const AssetID *sharedModelId = nullptr; ///< if set, alias this ID's model to the pointed-to source
};

// Shared source IDs referenced by entries below.
constexpr AssetID kPlayerWolfId = AssetID::PLAYER_WOLF;

static const AssetEntry kAssets[] = {
    {AssetID::PLAYER_WOLF,    ASSET_DIR "/UAL2_Standard.glb", ASSET_DIR "/UAL2_Standard.glb", nullptr},
    // ENEMY_ASHIGARU currently uses a debug cube renderer, so no model is
    // loaded for it. When it gets a real model, set modelPath and remove
    // sharedModelId — OR point sharedModelId at another entry to reuse that
    // model without a second GPU upload:
    //   {AssetID::ENEMY_ASHIGARU, nullptr, nullptr, &kPlayerWolfId},
    {AssetID::ENEMY_ASHIGARU, nullptr, nullptr, nullptr},
};
} // namespace

// ---------------------------------------------------------------------------
// GameRenderer
// ---------------------------------------------------------------------------
GameRenderer::GameRenderer() { initializeAssets(); }

void GameRenderer::initializeAssets() {
  // 1. Load assets declared in the manifest table.
  for (const auto &entry : kAssets) {
    if (entry.modelPath)
      assetManager.loadModel(entry.id, entry.modelPath);
    if (entry.animPath)
      assetManager.loadAnimations(entry.id, entry.animPath);
  }

  // 2. Register model aliases (sharing). Must run after all loads above.
  for (const auto &entry : kAssets) {
    if (entry.sharedModelId)
      assetManager.shareModel(entry.id, *entry.sharedModelId);
  }

  // 3. Register a rendering strategy for each AssetID.
  //    Swap DebugCubeRenderer for SkinnedEntityRenderer once an entity
  //    gets a real model and animations.
  entityRenderers[AssetID::PLAYER_WOLF] =
      std::make_unique<SkinnedEntityRenderer>();
  entityRenderers[AssetID::ENEMY_ASHIGARU] =
      std::make_unique<DebugCubeRenderer>(BLUE, BLACK);
}

void GameRenderer::renderGameplay(
    const CameraController &camera,
    const std::vector<CharacterRenderData> &entitiesToDraw) {

  ClearBackground(RAYWHITE);
  BeginMode3D(camera.getCamera());

  drawEnvironment();

  for (const auto &renderData : entitiesToDraw) {
    auto it = entityRenderers.find(renderData.assetId);
    if (it != entityRenderers.end()) {
      it->second->draw(assetManager, renderData);
    }
  }

  EndMode3D();

  // 2D UI overlay
  DrawFPS(10, 10);
  DrawText("Architecture Phase 2: Decoupled Renderer", 10, 40, 20, DARKGRAY);
}

void GameRenderer::drawEnvironment() { DrawGrid(300, 10.0f); }
