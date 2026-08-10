#include <Rendering/DebugCubeRenderer.h>
#include <Rendering/GameRenderer.h>
#include <Rendering/SkinnedEntityRenderer.h>

#include <Rendering/AssetManifest.h>

// ---------------------------------------------------------------------------
// Asset Manifest
// ---------------------------------------------------------------------------
// To add a new entity:
//   1. Add its AssetID to AssetID.h
//   2. Add one row to AssetManifest.h — model/anim paths (or nullptr) AND the renderer kind.
//      Loading, sharing, and renderer registration all flow from this table.
// ---------------------------------------------------------------------------
namespace {

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
GameRenderer::GameRenderer(AssetManager &assets) : assetManager(assets) {
  initializeAssets();
}

void GameRenderer::initializeAssets() {
  // Loading is now handled by LoadingState!
  // This function only registers the rendering strategies.

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
