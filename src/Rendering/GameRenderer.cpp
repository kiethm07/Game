#include <Rendering/DebugCubeRenderer.h>
#include <Core/AssetPaths.h>
#include <Rendering/GameRenderer.h>
#include <Rendering/ScopedMaterialShader.h>
#include <Rendering/ShaderLibrary.h>
#include <Rendering/SkinnedEntityRenderer.h>
#include <Util/Frustum.h>
#include <raymath.h>
#include <rlgl.h>
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
namespace {
/// Fallback floor, drawn only when the level has no visual mesh of its own.
///
/// A level authored in Blender brings its own ground, so this is not the normal
/// path any more — it is what keeps a collision-only greybox from rendering as
/// characters floating in a void with nothing for their shadows to land on.
constexpr float kGroundSize = 200.0f;
const Color kGroundColor = {170, 170, 165, 255};

/// Debug grid, kept in step with kGroundSize: 40 cells x 5m = 200m.
constexpr int kGridSlices = 40;
constexpr float kGridSpacing = 5.0f;

/// The grid and the fallback ground are coplanar at y=0 and would z-fight, so
/// one of them has to move. It is the ground that sinks rather than the grid
/// that lifts: a lifted grid puts its lines 1cm up the side of every obstacle,
/// where they read as a light fringe tracing each base. The grid marks the y=0
/// physics floor and belongs on it.
constexpr float kGroundSink = 0.01f;

/// How far out the detail mesh is still drawn, in metres.
///
/// Safe to cut here only because grass.fs fades a blade's vertex shading to
/// nothing between 30 and 70 m: past that fade every blade is already exactly
/// the terrain colour it stands on, so a chunk dropping out at 85 m removes
/// overdraw and nothing visible. Raise it if a distant ridge silhouetted
/// against the sky ever reads as bald.
constexpr float kGrassDrawDistance = 85.0f;

/// Is any part of `box` inside kGrassDrawDistance of `eye`?
///
/// Measured to the box rather than to its centre, so a 36 m chunk the camera is
/// standing at the near edge of is not cut for having a far centre.
bool withinGrassDistance(const BoundingBox &box, const Vector3 &eye) {
  const float dx = fmaxf(fmaxf(box.min.x - eye.x, 0.0f), eye.x - box.max.x);
  const float dy = fmaxf(fmaxf(box.min.y - eye.y, 0.0f), eye.y - box.max.y);
  const float dz = fmaxf(fmaxf(box.min.z - eye.z, 0.0f), eye.z - box.max.z);
  return dx * dx + dy * dy + dz * dz <=
         kGrassDrawDistance * kGrassDrawDistance;
}

/// raylib's built-in 1x1 white texture, as a Texture2D.
///
/// A glTF material with no baseColorTexture arrives with texture.id == 0, and
/// DrawMesh then binds nothing for that slot — so level.fs would sample
/// whatever texture the previous draw happened to leave bound. Substituting
/// white makes the sample a no-op and lets the material's baseColorFactor carry
/// the colour on its own, which is every material in the greybox.
/// Conservative caster bounds for a character, from its transform alone.
///
/// Deliberately generous — 1.5m out and 3m up from the feet, against a ~1.8m
/// humanoid. CharacterRenderData carries no collider dimensions, and the bind
/// pose's own bounding box would be wrong anyway: a lunge or an overhead swing
/// reaches well past a T-pose. Over-estimating a caster costs one draw call it
/// did not need; under-estimating pops a character's shadow out of existence
/// mid-animation, which is the far worse trade at this size.
BoundingBox characterCasterBox(const TransformData &transform) {
  constexpr float kReach = 1.5f;
  constexpr float kHeight = 3.0f;
  const Vector3 p = transform.position;
  return BoundingBox{{p.x - kReach, p.y - 0.5f, p.z - kReach},
                     {p.x + kReach, p.y + kHeight, p.z + kReach}};
}

Texture2D defaultWhiteTexture() {
  Texture2D texture{};
  texture.id = rlGetTextureIdDefault();
  texture.width = 1;
  texture.height = 1;
  texture.mipmaps = 1;
  texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  return texture;
}
} // namespace

GameRenderer::GameRenderer(AssetManager &assets, const Level &level)
    : assetManager(assets) {
  initializeAssets();
  loadLevelModel(level);
  loadDetailModel(level);
  shadowMap.setBounds(level.bounds);
}

GameRenderer::~GameRenderer() {
  if (worldShader.id != 0) UnloadShader(worldShader);
  if (levelShader.id != 0) UnloadShader(levelShader);
  if (grassShader.id != 0) UnloadShader(grassShader);
  if (hasDetailModel) {
    // Same reason as the level model below: UnloadModel would free grassShader
    // a second time through the material that points at it.
    for (int i = 0; i < detailModel.materialCount; i++) {
      detailModel.materials[i].shader = Shader{};
    }
    UnloadModel(detailModel);
  }
  if (hasLevelModel) {
    // The materials point at levelShader, which is unloaded just above.
    // UnloadModel would try to unload it a second time through the material,
    // so the shader is cleared off them first.
    for (int i = 0; i < levelModel.materialCount; i++) {
      levelModel.materials[i].shader = Shader{};
    }
    UnloadModel(levelModel);
  }
}

void GameRenderer::loadLevelModel(const Level &level) {
  if (level.visualModelPath.empty()) {
    TraceLog(LOG_INFO, "GameRenderer: level has no visual mesh; drawing the "
                       "fallback ground plane.");
    return;
  }

  levelModel = LoadModel(level.visualModelPath.c_str());
  if (levelModel.meshCount == 0) {
    TraceLog(LOG_ERROR,
             "GameRenderer: could not load level mesh '%s'. If it exports fine "
             "from Blender, check that Draco compression and KTX2 textures are "
             "both off — raylib's loader supports neither and returns an empty "
             "model rather than an error.",
             level.visualModelPath.c_str());
    UnloadModel(levelModel);
    levelModel = Model{};
    return;
  }

  const Texture2D white = defaultWhiteTexture();
  for (int i = 0; i < levelModel.materialCount; i++) {
    // Assigned onto the materials, not bound with BeginShaderMode: DrawMesh
    // reads material.shader and ignores whatever rlgl has bound.
    levelModel.materials[i].shader = levelShader;
    if (levelModel.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture.id == 0) {
      levelModel.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture = white;
    }
  }

  levelMeshTriangles.reserve(static_cast<size_t>(levelModel.meshCount));
  levelMeshBounds.reserve(static_cast<size_t>(levelModel.meshCount));
  int totalTriangles = 0;
  for (int i = 0; i < levelModel.meshCount; i++) {
    levelMeshTriangles.push_back(levelModel.meshes[i].triangleCount);
    levelMeshBounds.push_back(GetMeshBoundingBox(levelModel.meshes[i]));
    totalTriangles += levelModel.meshes[i].triangleCount;
  }

  hasLevelModel = true;
  TraceLog(LOG_INFO,
           "GameRenderer: level mesh '%s' — %d meshes, %d materials, %d "
           "triangles",
           level.visualModelPath.c_str(), levelModel.meshCount,
           levelModel.materialCount, totalTriangles);
}

void GameRenderer::loadDetailModel(const Level &level) {
  if (level.detailModelPath.empty()) return;

  detailModel = LoadModel(level.detailModelPath.c_str());
  if (detailModel.meshCount == 0) {
    TraceLog(LOG_WARNING,
             "GameRenderer: could not load detail mesh '%s'; the level will "
             "draw without its scenery.",
             level.detailModelPath.c_str());
    UnloadModel(detailModel);
    detailModel = Model{};
    return;
  }

  for (int i = 0; i < detailModel.materialCount; i++) {
    detailModel.materials[i].shader = grassShader;
    // No white-texture substitution, unlike loadLevelModel. grass.fs samples no
    // texture at all, so SHADER_LOC_MAP_DIFFUSE resolves to -1 and DrawMesh has
    // nothing to bind — the colour rides entirely on baseColorFactor.
  }

  detailMeshTriangles.reserve(static_cast<size_t>(detailModel.meshCount));
  detailMeshBounds.reserve(static_cast<size_t>(detailModel.meshCount));
  int totalTriangles = 0;
  for (int i = 0; i < detailModel.meshCount; i++) {
    detailMeshTriangles.push_back(detailModel.meshes[i].triangleCount);
    detailMeshBounds.push_back(GetMeshBoundingBox(detailModel.meshes[i]));
    totalTriangles += detailModel.meshes[i].triangleCount;
  }

  hasDetailModel = true;
  TraceLog(LOG_INFO,
           "GameRenderer: detail mesh '%s' — %d meshes, %d triangles, no "
           "shadow casting",
           level.detailModelPath.c_str(), detailModel.meshCount,
           totalTriangles);
}

void GameRenderer::initializeAssets() {
  // Models, animations and aliases are loaded by LoadingState before this
  // object exists — see the manifest walk there. Repeating those passes here
  // would only trip AssetManager's already-loaded guards, so this function
  // registers the renderer strategies and the environment shaders.

  // 1. Register a rendering strategy for each AssetID, straight from the table.
  for (const auto &entry : kAssets) {
    entityRenderers[entry.id] = makeRenderer(entry.renderer);
  }

  // 2. Environment shaders. Not owned by AssetManager: they belong to the
  //    environment, and AssetManager's shaders are keyed to a model's vertex
  //    format. Both pull in shadow_common.glsl, hence ShaderLibrary rather than
  //    raylib's LoadShader.
  worldShader = ShaderLibrary::load(assets::path("shaders/glsl330/world.vs"),
                                    assets::path("shaders/glsl330/world.fs"));
  if (worldShader.id == 0) {
    TraceLog(LOG_ERROR, "GameRenderer: failed to load world shader; obstacles "
                        "will draw unlit.");
  }

  levelShader = ShaderLibrary::load(assets::path("shaders/glsl330/level.vs"),
                                    assets::path("shaders/glsl330/level.fs"));
  if (levelShader.id == 0) {
    TraceLog(LOG_ERROR, "GameRenderer: failed to load level shader; the level "
                        "mesh will draw unlit.");
  }

  grassShader = ShaderLibrary::load(assets::path("shaders/glsl330/grass.vs"),
                                    assets::path("shaders/glsl330/grass.fs"));
  if (grassShader.id == 0) {
    // Deliberately no fallback to levelShader: its vertex stage declares
    // vertexTexCoord and vertexNormal, which the detail meshes do not carry, so
    // substituting it would draw the grass from undefined attributes rather
    // than not at all.
    TraceLog(LOG_ERROR, "GameRenderer: failed to load the grass shader; the "
                        "detail mesh will draw unlit.");
  }
}

ShadowMode GameRenderer::cycleShadowMode() {
  switch (shadowMode) {
  case ShadowMode::Full:     shadowMode = ShadowMode::NearOnly; break;
  case ShadowMode::NearOnly: shadowMode = ShadowMode::Off;      break;
  case ShadowMode::Off:
  default:                   shadowMode = ShadowMode::Full;     break;
  }
  return shadowMode;
}

void GameRenderer::renderShadowPass(
    const std::vector<PhysicsObstacle> &obstacles,
    const std::vector<CharacterRenderData> &entitiesToDraw, Vector3 focus) {
  ShadowStats &stats = shadowMap.stats();
  stats.reset();

  if (!shadowMap.isReady()) return;

  // Off skips the depth pass but leaves the maps bound and sampled, so the
  // difference against Full is the depth pass on its own. Whatever is already
  // in the depth textures stays there and goes stale, which is the intended
  // (and obvious) look for a debug mode.
  if (shadowMode == ShadowMode::Off) return;

  // Recentre the near cascade before anything is rendered from the light.
  shadowMap.update(focus);

  const int cascadeCount =
      (shadowMode == ShadowMode::NearOnly) ? 1 : ShadowMap::kCascadeCount;

  // Casters are culled per cascade against that cascade's ortho bounds. The
  // ortho projection would clip them anyway, but only after they have been
  // transformed and rasterized — and the near cascade covers 16m of what may be
  // a 150m level, so almost everything submitted to it used to be thrown away
  // downstream. See ShadowMap::casterVisible for why the test is XY-only.
  for (int cascade = 0; cascade < cascadeCount; cascade++) {
    shadowMap.beginDepthPass(cascade);

    // Static geometry. One BeginShaderMode around the whole set: rlSetShader
    // flushes the batch, so wrapping each obstacle individually would cost a
    // draw call per obstacle for nothing.
    //
    // The fallback ground is deliberately not a caster. A flat floor shadowing
    // itself adds nothing and only feeds acne.
    //
    // Obstacles cast only when they are what is on screen. Once a level mesh is
    // present the proxies sit *inside* the geometry they approximate, so having
    // both cast would lay a box-shaped shadow next to the real one wherever the
    // approximation is loose.
    if (!hasLevelModel) {
      BeginShaderMode(shadowMap.getStaticDepthShader());
      for (const PhysicsObstacle &obs : obstacles) {
        if (!shadowMap.casterVisible(cascade, obs.getApproxBox())) {
          stats.castersCulled[cascade]++;
          continue;
        }
        obs.drawSolid();
        stats.castersSubmitted[cascade]++;
        // A box is 12 triangles, a ramp 12 as well (rampCorners builds a
        // six-faced wedge). Close enough for a cost readout.
        stats.trianglesSubmitted[cascade] += 12;
      }
      EndShaderMode();
    }

    // The level mesh. Outside the BeginShaderMode scope above on purpose: it is
    // a Model, so it goes through DrawMesh, which reads material.shader and
    // ignores whatever rlgl has bound. The depth shader has to go onto the
    // materials instead — same reason the characters below do it.
    //
    // `detailModel` is absent from this function on purpose, and its absence is
    // the whole implementation of "the grass casts no shadow". It also keeps
    // ~148k blade triangles out of both cascades, which is why the depth pass
    // costs less than it would if the meadow lived in levelModel.
    if (hasLevelModel) {
      ScopedMaterialShader depthPass(levelModel,
                                     shadowMap.getStaticDepthShader());
      // Per mesh rather than one DrawModel, so meshes outside this cascade can
      // be skipped. Identity transform: raylib bakes glTF node transforms into
      // the vertex data at load, so the mesh already sits where Blender had it.
      const Matrix identity = MatrixIdentity();
      for (int i = 0; i < levelModel.meshCount; i++) {
        if (!shadowMap.casterVisible(cascade, levelMeshBounds[i])) {
          stats.castersCulled[cascade]++;
          continue;
        }
        DrawMesh(levelModel.meshes[i],
                 levelModel.materials[levelModel.meshMaterial[i]], identity);
        stats.castersSubmitted[cascade]++;
        stats.trianglesSubmitted[cascade] += levelMeshTriangles[i];
      }
    }

    // Characters. These do NOT go through BeginShaderMode —
    // SkinnedEntityRenderer swaps the depth shader onto the model's materials
    // instead, which is what keeps raylib uploading the bone matrices and makes
    // the shadow follow the pose. See the comment on ScopedMaterialShader.
    for (const auto &renderData : entitiesToDraw) {
      auto it = entityRenderers.find(renderData.assetId);
      if (it == entityRenderers.end()) continue;

      if (!shadowMap.casterVisible(cascade,
                                   characterCasterBox(renderData.transform))) {
        stats.castersCulled[cascade]++;
        continue;
      }

      it->second->draw(assetManager, renderData, RenderPass::ShadowDepth);
      stats.castersSubmitted[cascade]++;
      const Model &model = assetManager.getModel(renderData.assetId);
      for (int m = 0; m < model.meshCount; m++) {
        stats.trianglesSubmitted[cascade] += model.meshes[m].triangleCount;
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
  // Hand this frame's light matrix and depth texture to every receiving
  // shader. Once per frame, before anything is drawn with them.
  shadowMap.applyTo(worldShader);
  shadowMap.applyTo(levelShader);
  // The grass casts nothing but still receives, so a tree's shadow falls across
  // the meadow. Miss this line and it draws uniformly lit under every canopy.
  shadowMap.applyTo(grassShader);
  shadowMap.applyTo(assetManager.getSkinningShader());

  drawEnvironment(camera, obstacles);

  for (const auto &renderData : entitiesToDraw) {
    auto it = entityRenderers.find(renderData.assetId);
    if (it != entityRenderers.end()) {
      it->second->draw(assetManager, renderData, RenderPass::Scene);
    }
  }
}

namespace {
const char *shadowModeName(ShadowMode mode) {
  switch (mode) {
  case ShadowMode::NearOnly: return "near cascade only";
  case ShadowMode::Off:      return "OFF";
  case ShadowMode::Full:
  default:                   return "full";
  }
}

/// Rolling mean of frame time.
///
/// A single frame's GetFrameTime() is far too noisy to compare two shadow modes
/// against each other, and DrawFPS is quantised to whole frames per second --
/// under vsync it reads a flat 60 whether a frame costs 3ms or 16ms, which is
/// exactly the range this needs to resolve. Averaging over ~2 seconds is what
/// makes an A/B between F3 modes readable.
class FrameTimeAverage {
public:
  void sample(float dt) {
    total += dt - samples[cursor];
    samples[cursor] = dt;
    cursor = (cursor + 1) % kWindow;
    if (filled < kWindow) filled++;
  }
  float milliseconds() const {
    return filled == 0 ? 0.0f : (total / filled) * 1000.0f;
  }

private:
  static constexpr int kWindow = 120;
  float samples[kWindow]{};
  float total = 0.0f;
  int cursor = 0;
  int filled = 0;
};
} // namespace

void GameRenderer::drawUI() {
  static FrameTimeAverage frameTime;
  frameTime.sample(GetFrameTime());

  DrawFPS(10, 10);

  if (!debugOverlay) {
    DrawText("F2 collision overlay  F3 shadow mode", 10, 40, 16, GRAY);
    return;
  }

  const ShadowStats &stats = shadowMap.stats();
  int y = 40;
  const int line = 18;

  DrawText(TextFormat("frame %5.2f ms (avg)", frameTime.milliseconds()), 10, y,
           16, DARKGRAY);
  y += line;
  DrawText(TextFormat("shadows: %s  [F3]", shadowModeName(shadowMode)), 10, y,
           16, DARKGRAY);
  y += line;

  for (int i = 0; i < ShadowMap::kCascadeCount; i++) {
    DrawText(TextFormat("  cascade %d  %4.0fm  casters %3d (culled %3d)  tris %6d",
                        i, shadowMap.getExtent(i), stats.castersSubmitted[i],
                        stats.castersCulled[i], stats.trianglesSubmitted[i]),
             10, y, 16, DARKGRAY);
    y += line;
  }
}

void GameRenderer::drawShadowMapDebug() {
  // A depth texture through the default 2D shader reads as a red ramp. That is
  // enough to answer the only questions this is here for: is anything in each
  // map, is the far frustum framed over the arena, and is the near one tracking
  // the player?
  const float size = 200.0f;
  const float pad = 10.0f;
  static const char *kLabels[ShadowMap::kCascadeCount] = {"near", "far"};

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
    const CameraController &camera,
    const std::vector<PhysicsObstacle> &obstacles) {
  // The level mesh. Its materials already carry levelShader, so no
  // BeginShaderMode — that would not reach DrawMesh anyway.
  //
  // Culled against the camera frustum, mesh by mesh. rlgl's matrices are read
  // rather than rebuilt from the CameraController: the caller has already
  // opened the 3D scope, so these are by construction the exact matrices the
  // draws below will be transformed by, and a frustum derived from anything
  // else could disagree with them and cull geometry that is genuinely on
  // screen.
  if (hasLevelModel) {
    const Matrix viewProj =
        MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    const Frustum frustum = Frustum::fromViewProjection(viewProj);
    const Matrix identity = MatrixIdentity();

    for (int i = 0; i < levelModel.meshCount; i++) {
      if (!frustum.intersects(levelMeshBounds[i])) continue;
      DrawMesh(levelModel.meshes[i],
               levelModel.materials[levelModel.meshMaterial[i]], identity);
    }

    // The detail mesh, over the same frustum.
    //
    // Culling off because a blade is a single-sided triangle: with it on, half
    // of every blade in the meadow would vanish, and a different half as the
    // camera came round. Only lighting would suffer from that in level.fs's
    // scheme — grass.fs lights from a constant up normal instead, so both faces
    // of a blade already shade identically and there is no normal flip to go
    // with this.
    //
    // Opaque, and it writes depth, so draw order is not a correctness question
    // here the way it would be for cut-out foliage. The only cost of getting it
    // wrong is overdraw.
    if (hasDetailModel) {
      const Vector3 eye = camera.getCamera().position;
      rlDisableBackfaceCulling();
      for (int i = 0; i < detailModel.meshCount; i++) {
        if (!frustum.intersects(detailMeshBounds[i])) continue;
        if (!withinGrassDistance(detailMeshBounds[i], eye)) continue;
        DrawMesh(detailModel.meshes[i],
                 detailModel.materials[detailModel.meshMaterial[i]], identity);
      }
      rlEnableBackfaceCulling();
    }
  }

  // Obstacle solids. Only drawn when they are not already represented by the
  // level mesh: with a real level the proxies sit *inside* the geometry they
  // approximate, so drawing them would z-fight with it and paint flat debug
  // colours over the art. Without one they are the only thing there is.
  //
  // Everything in this scope goes through rlgl's immediate-mode batch, which is
  // exactly what world.vs is written for.
  if (!hasLevelModel) {
    BeginShaderMode(worldShader);
    DrawPlane({0.0f, -kGroundSink, 0.0f}, {kGroundSize, kGroundSize},
              kGroundColor);
    for (const PhysicsObstacle &obs : obstacles) {
      obs.drawSolid();
    }
    EndShaderMode();
  }

  // Collision overlay, unlit, on raylib's default shader. Off by default — this
  // is the alignment check between the JSON proxies and the exported mesh, not
  // something the game is meant to look like.
  if (debugOverlay) {
    for (const PhysicsObstacle &obs : obstacles) {
      obs.drawWires();
    }
    DrawGrid(kGridSlices, kGridSpacing);
  }
}
