#include <Rendering/GameRenderer.h>
#include <rlgl.h>
#include <raymath.h>

GameRenderer::GameRenderer() {
  initializeAssets();
}

void GameRenderer::initializeAssets() {
  // Load the player model and animations from the uploaded .glb file
  assetManager.loadModel(AssetID::PLAYER_WOLF, ASSET_DIR "/UAL2_Standard.glb");
  assetManager.loadAnimations(AssetID::PLAYER_WOLF, ASSET_DIR "/UAL2_Standard.glb");
}

void GameRenderer::renderGameplay(
    const CameraController &camera,
    const std::vector<CharacterRenderData> &entitiesToDraw) {
  ClearBackground(RAYWHITE);

  BeginMode3D(camera.getCamera());

  drawEnvironment();

  for (const auto &renderData : entitiesToDraw) {
    if (renderData.assetId == AssetID::PLAYER_WOLF) {
      // 1. Get model and animations
      Model& model = assetManager.getModel(renderData.assetId);
      
      int animCount = 0;
      ModelAnimation* anims = assetManager.getAnimations(renderData.assetId, animCount);
      
      // 2. Update animations if they exist
      if (anims != nullptr && animCount > 0) {
        int animIndex = renderData.currentAnimationIndex % animCount;
        int keyframeCount = anims[animIndex].keyframeCount;
        if (keyframeCount > 0) {
            int frame = renderData.currentAnimationFrame % keyframeCount;
            UpdateModelAnimation(model, anims[animIndex], frame);
        }
      }
      
      // 3. Draw the model.
      // The GLB was exported from Blender (Z-up), so the root bone has a -90deg X rotation baked in.
      // DrawModelEx only applies one rotation axis, so we must manually chain the transforms:
      //   a) Translate to player's world position
      //   b) Rotate for player facing direction (Y axis)
      // The Blender->glTF exporter already pre-corrects the coordinate system inside the mesh,
      // so we do NOT need to add an extra -90 X rotation here — DrawModelEx handles this correctly.
      // What we DO need is to strip the root bone's translation from the draw position,
      // as the root bone's world translation (baked into currentPose) is included in bone 0.
      Vector3 drawPosition = renderData.position;

      if (anims != nullptr && animCount > 0) {
        int animIndex = renderData.currentAnimationIndex % animCount;
        int keyframeCount = anims[animIndex].keyframeCount;
        if (keyframeCount > 0 && anims[animIndex].boneCount > 0) {
          int frame = renderData.currentAnimationFrame % keyframeCount;
          // The root bone (bone 0) carries the forward root motion in local model space.
          // We cancel its XZ translation so the entity stays at its physics position.
          Vector3 rootFrame0 = anims[animIndex].keyframePoses[0][0].translation;
          Vector3 rootFrameN = anims[animIndex].keyframePoses[frame][0].translation;
          // local offset in model space (model faces +Z, so its forward is Z)
          float localDeltaX = rootFrameN.x - rootFrame0.x;
          float localDeltaZ = rootFrameN.z - rootFrame0.z;
          // Rotate the local offset into world space by the character's facing angle
          float yawRad = renderData.rotation.y * DEG2RAD;
          float worldDeltaX = localDeltaX * cosf(yawRad) + localDeltaZ * sinf(yawRad);
          float worldDeltaZ = -localDeltaX * sinf(yawRad) + localDeltaZ * cosf(yawRad);
          drawPosition.x -= worldDeltaX;
          drawPosition.z -= worldDeltaZ;
        }
      }

      Vector3 rotationAxis = {0.0f, 1.0f, 0.0f};
      DrawModelEx(model, drawPosition, rotationAxis, renderData.rotation.y, renderData.scale, WHITE);

    } else if (renderData.assetId == AssetID::ENEMY_ASHIGARU) {
      // For now, keep the debug cube for the enemy
      rlPushMatrix();
      rlTranslatef(renderData.position.x, renderData.position.y,
                   renderData.position.z);
      rlRotatef(renderData.rotation.y, 0.0f, 1.0f, 0.0f);

      DrawCube({0.0f, 0.0f, 0.0f}, renderData.scale.x, renderData.scale.y,
               renderData.scale.z, RED);
      DrawCubeWires({0.0f, 0.0f, 0.0f}, renderData.scale.x, renderData.scale.y,
                    renderData.scale.z, BLACK);

      rlPopMatrix();
    }
  }

  EndMode3D();

  // 2D UI Overlay
  DrawFPS(10, 10);
  DrawText("Architecture Phase 2: Decoupled Renderer", 10, 40, 20, DARKGRAY);
}

void GameRenderer::drawEnvironment() { DrawGrid(300, 10.0f); }
