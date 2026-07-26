#include <Rendering/DebugCubeRenderer.h>
#include <Rendering/GameRenderer.h>
#include <Rendering/SkinnedEntityRenderer.h>

// ---------------------------------------------------------------------------
// Asset Manifest
// ---------------------------------------------------------------------------
// To add a new entity:
//   1. Add its AssetID to AssetID.h
//   2. Add one row here — model/anim paths (or nullptr) AND the renderer kind.
//      Loading, sharing, and renderer registration all flow from this table.
// ---------------------------------------------------------------------------
namespace {
/// Which IEntityRenderer strategy an asset is drawn with.
enum class RendererKind {
  SkinnedCharacter, ///< SkinnedEntityRenderer (GPU-skinned GLB)
  DebugCube,        ///< DebugCubeRenderer (placeholder proxy)
};

struct AssetEntry {
  AssetID id;
  const char *modelPath; ///< nullptr → no model to load
  const char *animPath;  ///< nullptr → no animations to load
  RendererKind renderer = RendererKind::SkinnedCharacter;
  const AssetID *sharedModelId =
      nullptr; ///< if set, alias model to pointed-to source
  const AssetID *sharedAnimId =
      nullptr; ///< if set, alias animations to pointed-to source
};

static const AssetEntry kAssets[] = {
    {AssetID::PLAYER_WOLF, ASSET_DIR "/Alpha_Character.glb",
     ASSET_DIR "/Alpha_Character.glb", RendererKind::SkinnedCharacter},
    {AssetID::ENEMY_ASHIGARU, ASSET_DIR "/Walk.glb", ASSET_DIR "/Walk.glb",
     RendererKind::SkinnedCharacter},
};

std::unique_ptr<IEntityRenderer> makeRenderer(RendererKind kind) {
  switch (kind) {
  case RendererKind::DebugCube:
    return std::make_unique<DebugCubeRenderer>();
  case RendererKind::SkinnedCharacter:
  default:
    return std::make_unique<SkinnedEntityRenderer>();
  }
}
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

  // 2. Register model and animation aliases (sharing). Must run after all loads
  // above.
  for (const auto &entry : kAssets) {
    if (entry.sharedModelId)
      assetManager.shareModel(entry.id, *entry.sharedModelId);
    if (entry.sharedAnimId)
      assetManager.shareAnimations(entry.id, *entry.sharedAnimId);
  }

  // 3. Register a rendering strategy for each AssetID, straight from the table.
  for (const auto &entry : kAssets) {
    entityRenderers[entry.id] = makeRenderer(entry.renderer);
  }
}

void GameRenderer::renderGameplay(
    const CameraController &camera,
    const std::vector<CharacterRenderData> &entitiesToDraw) {
  // Draws into the caller's active 3D scope. The caller owns
  // ClearBackground / BeginMode3D / EndMode3D and calls drawUI() afterwards.
  drawWorld(camera, entitiesToDraw);
}

void GameRenderer::drawWorld(
    const CameraController &camera,
    const std::vector<CharacterRenderData> &entitiesToDraw) {
  drawEnvironment();

  for (const auto &renderData : entitiesToDraw) {
    auto it = entityRenderers.find(renderData.assetId);
    if (it != entityRenderers.end()) {
      it->second->draw(assetManager, renderData);
    }
  }
}

void GameRenderer::drawUI() {
  DrawFPS(10, 10);
  DrawText("Architecture Phase 2: Decoupled Renderer", 10, 40, 20, DARKGRAY);
}

void GameRenderer::drawEnvironment() { DrawGrid(300, 10.0f); }
