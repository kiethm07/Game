#include <Rendering/DebugCubeRenderer.h>
#include <Rendering/GameRenderer.h>
#include <Rendering/SkinnedEntityRenderer.h>
#include <rlgl.h>

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

// The player asset is built in two passes, both from ~/Documents/3D/pack.blend:
//   1. tools/merge_animations.py folds the 51 per-clip Mixamo armatures into
//      one skinned GLB at scale 1.0 -> Paladin.glb
//   2. tools/bake_root_motion.py moves each clip's horizontal travel from
//      mixamorig:Hips onto a dedicated `Root` bone -> Paladin.rootmotion.glb
// Load only the second. Pointing this at Paladin.glb reverts bone 0 to the hips
// and root motion starts picking up hip sway.

// The ashigaru has no model of its own yet, so it borrows the player's rather
// than the single-clip Walk.glb it used to draw with: one skeleton, one set of
// 58 named clips, which is what lets SwordmanAnimator's table name "Slash" and
// "Impact_2" the way the player's does. Aliased, not loaded a second time —
// both IDs resolve to the one Model and the one animation array, and the
// skinning shader re-uploads the bone matrices per draw, so each entity poses
// it independently.
//
// Swapping in a real ashigaru asset is this pointer becoming a path, plus
// whatever clip names the new asset carries.
static const AssetID kAshigaruSource = AssetID::PLAYER_WOLF;

static const AssetEntry kAssets[] = {
    {AssetID::PLAYER_WOLF, ASSET_DIR "/Paladin.rootmotion.glb",
     ASSET_DIR "/Paladin.rootmotion.glb", RendererKind::SkinnedCharacter},
    {AssetID::ENEMY_ASHIGARU, nullptr, nullptr, RendererKind::SkinnedCharacter,
     &kAshigaruSource, &kAshigaruSource},
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
namespace {
/// Solid floor for shadows to land on. The world had no ground mesh at all
/// before — just DrawGrid's wire lines — so a character's shadow had nothing to
/// fall onto. 200m matches the ground plate the navmesh is built against
/// (GameplayState). Past the 48m shadow frustum the receiving shaders'
/// out-of-bounds guard reports "lit", so the outskirts simply stay unshadowed.
constexpr float kGroundSize = 200.0f;
const Color kGroundColor = {170, 170, 165, 255};

/// Kept in step with kGroundSize: 40 cells x 5m = 200m.
constexpr int kGridSlices = 40;
constexpr float kGridSpacing = 5.0f;

/// The grid and the ground plane are coplanar at y=0 and would z-fight, so one
/// of them has to move. It is the ground that sinks rather than the grid that
/// lifts: a lifted grid puts its lines 1cm up the side of every obstacle, where
/// they read as a light fringe tracing each base. The grid marks the y=0 physics
/// floor and belongs on it; the ground plane is purely decorative (the floor
/// itself is a hard clamp in PhysicsManager), so it is the one free to move.
constexpr float kGroundSink = 0.01f;
} // namespace

GameRenderer::GameRenderer(AssetManager &assets) : assetManager(assets) {
  initializeAssets();
}

GameRenderer::~GameRenderer() {
  if (worldShader.id != 0) UnloadShader(worldShader);
}

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

  // 4. Shader for the ground and the obstacles. Not owned by AssetManager: it
  //    belongs to the environment, and AssetManager's shaders are keyed to a
  //    model's vertex format.
  worldShader = LoadShader(ASSET_DIR "/shaders/glsl330/world.vs",
                           ASSET_DIR "/shaders/glsl330/world.fs");
  if (worldShader.id == 0) {
    TraceLog(LOG_ERROR, "GameRenderer: failed to load world shader; level "
                        "geometry will draw unlit.");
  }
}

void GameRenderer::renderShadowPass(
    const std::vector<PhysicsObstacle> &obstacles,
    const std::vector<CharacterRenderData> &entitiesToDraw, Vector3 focus) {
  if (!shadowMap.isReady()) return;

  // Recentre the near cascade before anything is rendered from the light.
  shadowMap.update(focus);

  // Every caster goes into every cascade. No culling by cascade: the depth
  // pass is a handful of boxes and four characters, and an ortho projection
  // already clips whatever falls outside each frustum.
  for (int cascade = 0; cascade < ShadowMap::kCascadeCount; cascade++) {
    shadowMap.beginDepthPass(cascade);

    // Static geometry. One BeginShaderMode around the whole set: rlSetShader
    // flushes the batch, so wrapping each obstacle individually would cost a
    // draw call per obstacle for nothing.
    //
    // The ground is deliberately not a caster. A flat floor shadowing itself
    // adds nothing and only feeds acne.
    BeginShaderMode(shadowMap.getStaticDepthShader());
    for (const PhysicsObstacle &obs : obstacles) {
      obs.drawSolid();
    }
    EndShaderMode();

    // Characters. These do NOT go through BeginShaderMode —
    // SkinnedEntityRenderer swaps the depth shader onto the model's materials
    // instead, which is what keeps raylib uploading the bone matrices and makes
    // the shadow follow the pose. See the comment on ScopedMaterialShader.
    for (const auto &renderData : entitiesToDraw) {
      auto it = entityRenderers.find(renderData.assetId);
      if (it != entityRenderers.end()) {
        it->second->draw(assetManager, renderData, RenderPass::ShadowDepth);
      }
    }

    shadowMap.endDepthPass();
  }
}

void GameRenderer::renderGameplay(
    const CameraController &camera,
    const std::vector<PhysicsObstacle> &obstacles,
    const std::vector<CharacterRenderData> &entitiesToDraw) {
  // Draws into the caller's active 3D scope. The caller owns
  // ClearBackground / BeginMode3D / EndMode3D and calls drawUI() afterwards.
  // renderShadowPass() must already have run this frame.
  drawWorld(camera, obstacles, entitiesToDraw);
}

void GameRenderer::drawWorld(
    const CameraController &camera,
    const std::vector<PhysicsObstacle> &obstacles,
    const std::vector<CharacterRenderData> &entitiesToDraw) {
  // Hand this frame's light matrix and depth texture to both receiving
  // shaders. Once per frame, before anything is drawn with them.
  shadowMap.applyTo(worldShader);
  shadowMap.applyTo(assetManager.getSkinningShader());

  drawEnvironment(obstacles);

  for (const auto &renderData : entitiesToDraw) {
    auto it = entityRenderers.find(renderData.assetId);
    if (it != entityRenderers.end()) {
      it->second->draw(assetManager, renderData, RenderPass::Scene);
    }
  }
}

void GameRenderer::drawUI() {
  DrawFPS(10, 10);
  DrawText("Architecture Phase 2: Decoupled Renderer", 10, 40, 20, DARKGRAY);
}

void GameRenderer::drawShadowMapDebug() {
  // A depth texture through the default 2D shader reads as a red ramp. That is
  // enough to answer the only questions this is here for: is anything in each
  // map, is the far frustum framed over the arena, and is the near one tracking
  // the player?
  const float size = 200.0f;
  const float pad = 10.0f;
  static const char *kLabels[ShadowMap::kCascadeCount] = {"near 16m", "far 48m"};

  for (int i = 0; i < ShadowMap::kCascadeCount; i++) {
    Texture2D depth = shadowMap.getDepthTexture(i);
    if (depth.id == 0) continue;

    // Right-aligned, near cascade first so it sits closest to the corner.
    float x = GetScreenWidth() - (size + pad) * (i + 1);
    DrawTexturePro(depth, {0, 0, (float)depth.width, (float)depth.height},
                   {x, pad, size, size}, {0, 0}, 0.0f, WHITE);
    DrawRectangleLines((int)x, (int)pad, (int)size, (int)size, DARKGRAY);
    DrawText(kLabels[i], (int)x + 4, (int)(pad + size) + 4, 10, DARKGRAY);
  }
}

void GameRenderer::drawEnvironment(
    const std::vector<PhysicsObstacle> &obstacles) {
  // Lit, shadow-receiving geometry. Everything here goes through rlgl's
  // immediate-mode batch, which is exactly what world.vs is written for.
  BeginShaderMode(worldShader);
  DrawPlane({0.0f, -kGroundSink, 0.0f}, {kGroundSize, kGroundSize}, kGroundColor);
  for (const PhysicsObstacle &obs : obstacles) {
    obs.drawSolid();
  }
  EndShaderMode();

  // Unlit overlays, back on raylib's default shader.
  for (const PhysicsObstacle &obs : obstacles) {
    obs.drawWires();
  }
  DrawGrid(kGridSlices, kGridSpacing);
}
