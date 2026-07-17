#include <Rendering/GameRenderer.h>
#include <rlgl.h>

GameRenderer::GameRenderer() {
  // You can call initializeAssets() here or let the Game level do it
}

void GameRenderer::initializeAssets() {
  // We will load our models here in the future
  // e.g. assetManager.loadModel(AssetID::PLAYER_WOLF,
  // "assets/models/wolf.iqm"); e.g.
  // assetManager.loadAnimations(AssetID::PLAYER_WOLF,
  // "assets/animations/wolf.iqm");
}

void GameRenderer::renderGameplay(
    const CameraController &camera,
    const std::vector<CharacterRenderData> &entitiesToDraw) {
  ClearBackground(RAYWHITE);

  BeginMode3D(camera.getCamera());

  drawEnvironment();

  for (const auto &renderData : entitiesToDraw) {
    // In the future when we actually have models, this will be:
    // Model& model = assetManager.getModel(renderData.assetId);
    // ... update animations ...
    // DrawModelEx(model, ...);

    // For now, to keep the current visual behavior without breaking anything:
    rlPushMatrix();
    rlTranslatef(renderData.position.x, renderData.position.y,
                 renderData.position.z);
    rlRotatef(renderData.rotation.y, 0.0f, 1.0f, 0.0f);

    // Draw a debug cube to represent the character
    if (renderData.assetId == AssetID::PLAYER_WOLF) {
      DrawCube({0.0f, 0.0f, 0.0f}, renderData.scale.x, renderData.scale.y,
               renderData.scale.z, BLUE);
      DrawCubeWires({0.0f, 0.0f, 0.0f}, renderData.scale.x, renderData.scale.y,
                    renderData.scale.z, BLACK);
    } else if (renderData.assetId == AssetID::ENEMY_ASHIGARU) {
      DrawCube({0.0f, 0.0f, 0.0f}, renderData.scale.x, renderData.scale.y,
               renderData.scale.z, RED);
      DrawCubeWires({0.0f, 0.0f, 0.0f}, renderData.scale.x, renderData.scale.y,
                    renderData.scale.z, BLACK);
    }

    rlPopMatrix();
  }

  EndMode3D();

  // 2D UI Overlay
  DrawFPS(10, 10);
  DrawText("Architecture Phase 2: Decoupled Renderer", 10, 40, 20, DARKGRAY);
}

void GameRenderer::drawEnvironment() { DrawGrid(300, 10.0f); }
