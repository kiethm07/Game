#pragma once

#include <Components/PhysicsObstacle.h>
#include <Core/CameraController.h>
#include <Level/Checkpoint.h>
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

  /// F3: what the depth pass is allowed to render.
  ///
  /// Cycling this in-game is how the depth pass gets measured. `Off` still
  /// leaves the scene shaders sampling the maps, so Full-minus-Off isolates the
  /// depth pass from the fragment-side PCF — the two scale with completely
  /// different things (map size vs screen resolution) and conflating them is
  /// how you end up optimising the wrong one.
  void setShadowMode(ShadowMode mode) { shadowMode = mode; }
  ShadowMode getShadowMode() const { return shadowMode; }
  /// Advance to the next mode, wrapping. Returns the new mode.
  ShadowMode cycleShadowMode();

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

  /// Lit, shadow-receiving shader for the detail mesh. Same lighting constants
  /// as level.fs, deliberately, so a blade and the ground under it come out the
  /// same colour — but no texture, no alpha test, and a constant up normal
  /// rather than the geometry's. See grass.fs.
  Shader grassShader{};

  /// Location of grass.vs's `time` uniform, resolved once at load.
  ///
  /// Cached because it is set every frame and GetShaderLocation is a driver
  /// string lookup. -1 when the shader failed to load or the uniform was
  /// optimised out; SetShaderValue ignores -1, so the meadow just stands
  /// still rather than the frame erroring.
  int grassTimeLoc = -1;

  /// Unlit shader for the campfire flame. Unlit because fire produces light
  /// rather than receiving it — run through levelShader the flame would dim in
  /// shade. See flame.fs.
  Shader flameShader{};

  /// The level's visual mesh. Owned here rather than by AssetManager, whose
  /// cache is keyed on the AssetID enum — one entry per level would mean an
  /// enum edit and a rebuild per map, which is exactly what moving levels into
  /// data was meant to stop.
  Model levelModel{};
  bool hasLevelModel = false;

  bool debugOverlay = false;
  ShadowMode shadowMode = ShadowMode::Full;

  /// Triangle count per mesh of the level model, so the depth-pass stats can
  /// report geometry submitted rather than just draw calls. Filled in
  /// loadLevelModel; parallel to levelModel.meshes.
  std::vector<int> levelMeshTriangles;

  /// World AABB per mesh of the level model, for both the per-cascade caster
  /// cull and the camera frustum cull. Computed once at load: GetMeshBoundingBox
  /// walks every vertex, which is fine on a 182k-triangle map once and not
  /// sixty times a second.
  ///
  /// World-space with no transform applied, because raylib bakes glTF node
  /// transforms into the vertex data and the model is drawn at identity.
  std::vector<BoundingBox> levelMeshBounds;

  /// The level's detail mesh: drawn, never collided, never a shadow caster.
  ///
  /// Its own Model rather than meshes inside levelModel because raylib's glTF
  /// loader discards mesh and material names, so there would be nothing to tell
  /// grass from terrain by at runtime — and because "casts no shadow" is
  /// implemented by renderShadowPass simply never naming this model.
  Model detailModel{};
  bool hasDetailModel = false;
  std::vector<BoundingBox> detailMeshBounds;
  std::vector<int> detailMeshTriangles;

  /// 3D world pass: environment + every entity's skinned/proxy geometry.
  void drawWorld(const CameraController &camera,
                 const std::vector<PhysicsObstacle> &obstacles,
                 const std::vector<CharacterRenderData> &entitiesToDraw);
  void drawEnvironment(const CameraController &camera,
                       const std::vector<PhysicsObstacle> &obstacles);

  /// The flames, drawn last and additively. Separate from drawEnvironment
  /// because they must come after the characters: an additive flame writes no
  /// depth, so anything drawn afterwards would paint straight over it.
  void drawFlames();

  /// Load the level's mesh and point its materials at levelShader. Also
  /// substitutes raylib's default white texture for any material that arrived
  /// without an albedo map, so level.fs samples white instead of whatever
  /// happened to be bound.
  void loadLevelModel(const Level &level);

  /// Load the level's detail mesh and point its materials at grassShader.
  /// Unlike loadLevelModel this substitutes no white texture, because nothing
  /// in grass.fs samples one.
  void loadDetailModel(const Level &level);

  /// The campfire, as two models: the body is opaque and lit like any prop,
  /// the flame is four nested shells drawn additively. They are separate files
  /// because they are drawn differently, not merely toggled — see
  /// tools/make_campfire.py.
  Model campfireBody{};
  Model campfireFlame{};
  bool hasCampfireModels = false;
  void loadCampfireModels();

  /// Where the campfires are and which are alight. Copied in rather than
  /// passed per frame: they change when one is lit, not every frame.
  std::vector<Checkpoint> checkpoints;

public:
  void setCheckpoints(const std::vector<Checkpoint> &points) {
    checkpoints = points;
  }

private:
};

