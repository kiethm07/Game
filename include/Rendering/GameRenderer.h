#pragma once

#include <Components/PhysicsObstacle.h>
#include <Core/CameraController.h>
#include <Level/Level.h>
#include <Rendering/AssetManager.h>
#include <Rendering/IEntityRenderer.h>
#include <Rendering/RenderData.h>
#include <Rendering/ShadowMap.h>
#include <memory>
#include <unordered_map>
#include <vector>

class GameRenderer {
public:
  /// Does not own the asset store. Root motion is gameplay data as much as it
  /// is render data, so the owner sits above both (see GameplayState) and hands
  /// the same AssetManager to entities through UpdateContext.
  ///
  /// The level is read for its visual mesh and its bounds and is not retained;
  /// the caller keeps owning it, and its obstacles arrive per-frame through the
  /// render calls below rather than being cached here.
  GameRenderer(AssetManager &assets, const Level &level);
  ~GameRenderer();

  GameRenderer(const GameRenderer &) = delete;
  GameRenderer &operator=(const GameRenderer &) = delete;

  void initializeAssets();

  /// F2: draw the collision proxies as wireframes over the level mesh, plus the
  /// world grid.
  ///
  /// This is the check that the Blender export lined up: the proxies come from
  /// level.json via this engine's own coordinate conversion, the mesh comes
  /// from the same .blend via Blender's glTF exporter, and the two only
  /// coincide if both conversions agree. A uniform rotation offset means a yaw
  /// sign is wrong; a mirrored layout means the Y/Z swap is.
  void setDebugOverlay(bool on) { debugOverlay = on; }
  bool debugOverlayEnabled() const { return debugOverlay; }

  /// PASS 1 — depth from the light's point of view, once per shadow cascade.
  ///
  /// Renders into its own framebuffers, so it must run *before* the frame's
  /// ClearBackground/BeginMode3D and must be handed the same entity list the
  /// scene pass will get. Opens and closes its own 3D scopes.
  ///
  /// `focus` is what the near cascade recentres on — the player. Passed in
  /// rather than read off entitiesToDraw[0]: that index is an ordering
  /// coincidence in GameplayState, not a contract.
  void renderShadowPass(const std::vector<PhysicsObstacle> &obstacles,
                        const std::vector<CharacterRenderData> &entitiesToDraw,
                        Vector3 focus);

  /// PASS 2 — the shaded 3D world. Assumes the caller has already opened a 3D
  /// scope (BeginMode3D) and will close it; draws environment + entities into it.
  void renderGameplay(const CameraController &camera,
                      const std::vector<PhysicsObstacle> &obstacles,
                      const std::vector<CharacterRenderData> &entitiesToDraw);

  /// 2D overlay pass: HUD/debug text. Must be called after EndMode3D.
  void drawUI();

  /// Corner overlay of the raw shadow map, for checking that the light frustum
  /// is framed over the arena. Must be called after EndMode3D.
  void drawShadowMapDebug();

private:
  AssetManager &assetManager;

  /// Maps each AssetID to the renderer strategy responsible for drawing it.
  std::unordered_map<AssetID, std::unique_ptr<IEntityRenderer>> entityRenderers;

  ShadowMap shadowMap;

  /// Lit, shadow-receiving shader for every obstacle. Distinct from the
  /// character shader because this geometry comes through rlgl immediate mode,
  /// already in world space — see world.vs.
  Shader worldShader{};

  /// Same lighting as worldShader, but for a Model rather than the
  /// immediate-mode batch, and with an albedo texture. See level.vs for why the
  /// two cannot share a vertex stage.
  Shader levelShader{};

  /// The level's visual mesh. Owned here rather than by AssetManager, whose
  /// cache is keyed on the AssetID enum — one entry per level would mean an
  /// enum edit and a rebuild per map, which is exactly what moving levels into
  /// data was meant to stop.
  Model levelModel{};
  bool hasLevelModel = false;

  bool debugOverlay = false;

  /// 3D world pass: environment + every entity's skinned/proxy geometry.
  void drawWorld(const CameraController &camera,
                 const std::vector<PhysicsObstacle> &obstacles,
                 const std::vector<CharacterRenderData> &entitiesToDraw);
  void drawEnvironment(const std::vector<PhysicsObstacle> &obstacles);

  /// Load the level's mesh and point its materials at levelShader. Also
  /// substitutes raylib's default white texture for any material that arrived
  /// without an albedo map, so level.fs samples white instead of whatever
  /// happened to be bound.
  void loadLevelModel(const Level &level);
};

