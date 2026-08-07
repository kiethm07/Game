#pragma once

#include <Components/PhysicsObstacle.h>
#include <Core/CameraController.h>
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
  explicit GameRenderer(AssetManager &assets);
  ~GameRenderer();

  GameRenderer(const GameRenderer &) = delete;
  GameRenderer &operator=(const GameRenderer &) = delete;

  void initializeAssets();

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

  /// Lit, shadow-receiving shader for the ground plane and every obstacle.
  /// Distinct from the character shader because this geometry comes through
  /// rlgl immediate mode, already in world space — see world.vs.
  Shader worldShader{};

  /// 3D world pass: environment + every entity's skinned/proxy geometry.
  void drawWorld(const CameraController &camera,
                 const std::vector<PhysicsObstacle> &obstacles,
                 const std::vector<CharacterRenderData> &entitiesToDraw);
  void drawEnvironment(const std::vector<PhysicsObstacle> &obstacles);
};

