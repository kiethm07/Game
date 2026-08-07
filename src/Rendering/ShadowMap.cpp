#include <Rendering/ShadowMap.h>

#include <cmath>
#include <raymath.h>
#include <rlgl.h>

namespace {

constexpr int kResolution = 2048;

/// Cascade extents. The near one is sized so a texel lands at roughly one
/// screen pixel at the camera's orbit distance: 16m/2048 = 7.8mm, against
/// ~4.8mm per screen pixel at CLOSE_DISTANCE. The far one covers the authored
/// arena (x in [-13, 15], z in [-15, 14]) with margin.
constexpr float kNearExtent = 16.0f;
constexpr float kFarExtent = 48.0f;

/// The far cascade never moves, so distant shadows never shimmer.
const Vector3 kArenaCentre = {0.0f, 0.0f, 0.0f};

/// How far back along the light each ortho camera sits, and the depth slab it
/// captures. Left at BeginMode3D's defaults the slab would be 0.05..4000, and
/// since orthographic depth is linear, a bias of 1e-4 would then mean 40cm of
/// world depth -- enough peter-panning to detach every shadow from its caster.
/// 1..121 keeps the depth units small enough for the shaders' world-space bias
/// conversion to mean what it says. +-60m along the light also clears the
/// tallest caster in the level (the 6m pillar) many times over.
constexpr float kLightDistance = 60.0f;
constexpr float kNearPlane = 1.0f;
constexpr float kFarPlane = 121.0f;

/// Same value the character shader used to hardcode, so promoting it to a
/// uniform did not shift the existing look. This is now the one place it lives.
const Vector3 kLightDirection = {-0.35f, -1.0f, -0.55f};

/// Texture units the depth maps are bound to. Deliberately above
/// MAX_MATERIAL_MAPS (12): DrawMesh walks slots 0..11 binding a material's
/// maps, so anything below that could be clobbered mid-frame the moment a
/// model gains a second texture.
constexpr int kNearSlot = 15;
constexpr int kFarSlot = 14;

/// Load a framebuffer with a depth texture and no colour attachment.
/// Lifted from raylib's examples/shaders/shaders_shadowmap_rendering.c.
RenderTexture2D loadDepthTarget(int width, int height) {
  RenderTexture2D target = {0};

  target.id = rlLoadFramebuffer();
  target.texture.width = width;
  target.texture.height = height;

  if (target.id == 0) {
    TraceLog(LOG_WARNING, "ShadowMap: could not create framebuffer object");
    return target;
  }

  rlEnableFramebuffer(target.id);

  target.depth.id = rlLoadTextureDepth(width, height, false);
  target.depth.width = width;
  target.depth.height = height;
  target.depth.format = 19; // DEPTH_COMPONENT_24BIT
  target.depth.mipmaps = 1;

  rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH,
                      RL_ATTACHMENT_TEXTURE2D, 0);

  if (!rlFramebufferComplete(target.id)) {
    TraceLog(LOG_WARNING, "ShadowMap: framebuffer [ID %i] is incomplete",
             target.id);
  }

  rlDisableFramebuffer();
  return target;
}

} // namespace

ShadowMap::ShadowMap() {
  lightDirection = Vector3Normalize(kLightDirection);

  cascades[0].extent = kNearExtent;
  cascades[0].textureSlot = kNearSlot;
  cascades[1].extent = kFarExtent;
  cascades[1].textureSlot = kFarSlot;

  staticDepthShader = LoadShader(ASSET_DIR "/shaders/glsl330/depth.vs",
                                 ASSET_DIR "/shaders/glsl330/depth.fs");

  ready = (staticDepthShader.id != 0);
  for (int i = 0; i < kCascadeCount; i++) {
    cascades[i].target = loadDepthTarget(kResolution, kResolution);
    ready = ready && (cascades[i].target.id != 0) &&
            (cascades[i].target.depth.id != 0);
    // Static framing up front, so a missed update() still renders something
    // sane rather than a frustum parked at the origin of an uninitialised camera.
    computeCascade(i, kArenaCentre);
  }

  if (!ready) {
    TraceLog(LOG_ERROR,
             "ShadowMap: unavailable; the scene will render unshadowed.");
  } else {
    TraceLog(LOG_INFO,
             "ShadowMap: %d cascades at %dx%d -- near %.0fm (%.1fmm/texel), "
             "far %.0fm (%.1fmm/texel)",
             kCascadeCount, kResolution, kResolution, kNearExtent,
             1000.0f * kNearExtent / kResolution, kFarExtent,
             1000.0f * kFarExtent / kResolution);
  }
}

ShadowMap::~ShadowMap() {
  if (staticDepthShader.id != 0) UnloadShader(staticDepthShader);
  for (int i = 0; i < kCascadeCount; i++) {
    // Deletes the attached depth texture along with the framebuffer.
    if (cascades[i].target.id != 0) rlUnloadFramebuffer(cascades[i].target.id);
  }
}

Texture2D ShadowMap::getDepthTexture(int cascade) const {
  if (cascade < 0 || cascade >= kCascadeCount) return Texture2D{};
  return cascades[cascade].target.depth;
}

void ShadowMap::computeCascade(int index, Vector3 focus) {
  Cascade &c = cascades[index];

  // Cascade 1 is anchored to the arena and ignores the focus entirely, so
  // distant shadows are perfectly stable no matter where the player walks.
  Vector3 centre = (index == 0) ? focus : kArenaCentre;

  if (index == 0) {
    // Texel snapping. Without this the near cascade's texel grid slides
    // continuously as the player moves, and every shadow edge in the scene
    // crawls and fizzes by a texel each frame.
    //
    // The snap has to happen on the light's own axes, not the world's: the
    // grid being quantised is the shadow map's, which is oriented by the light.
    // Snapping world X/Z instead leaves the grid sliding diagonally and looks
    // like the bug is still there.
    const Vector3 up = {0.0f, 1.0f, 0.0f};
    const Matrix basis = MatrixLookAt(Vector3Zero(), lightDirection, up);
    const float texel = c.extent / kResolution;

    Vector3 ls = Vector3Transform(centre, basis);
    ls.x = std::floor(ls.x / texel) * texel;
    ls.y = std::floor(ls.y / texel) * texel;
    // ls.z runs along the light; quantising it would do nothing for the grid
    // and would only jitter the depth slab.
    centre = Vector3Transform(ls, MatrixInvert(basis));
  }

  // Up is +Y. Safe as long as the light is not straight down, which the
  // authored direction is not (it leans -0.35 in x and -0.55 in z).
  c.camera.position =
      Vector3Subtract(centre, Vector3Scale(lightDirection, kLightDistance));
  c.camera.target = centre;
  c.camera.up = {0.0f, 1.0f, 0.0f};
  c.camera.projection = CAMERA_ORTHOGRAPHIC;
  c.camera.fovy = c.extent; // unused: the projection is replaced in beginDepthPass

  const float half = c.extent * 0.5f;
  c.projection = MatrixOrtho(-half, half, -half, half, kNearPlane, kFarPlane);
}

void ShadowMap::update(Vector3 focus) { computeCascade(0, focus); }

void ShadowMap::beginDepthPass(int cascade) {
  Cascade &c = cascades[cascade];

  BeginTextureMode(c.target);
  ClearBackground(WHITE); // no colour attachment; this is here for the depth clear
  BeginMode3D(c.camera);

  // BeginMode3D just built an orthographic projection from camera.fovy across
  // the near/far cull distances (0.05..4000). Replace it wholesale with the
  // tight slab -- rlSetMatrixProjection writes the same state slot that
  // BeginMode3D pushed, so EndMode3D still restores the caller's projection.
  rlSetMatrixProjection(c.projection);

  c.viewProj = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
}

void ShadowMap::endDepthPass() {
  EndMode3D();
  EndTextureMode();
}

const ShadowMap::UniformLocs &ShadowMap::locsFor(const Shader &target) {
  auto it = uniformCache.find(target.id);
  if (it != uniformCache.end()) return it->second;

  UniformLocs locs;
  // GLSL array uniforms resolve by their first element.
  locs.lightVP = GetShaderLocation(target, "lightVP[0]");
  locs.texelWorld = GetShaderLocation(target, "shadowTexelWorld[0]");
  locs.resolution = GetShaderLocation(target, "shadowMapResolution");
  locs.lightDir = GetShaderLocation(target, "lightDir");
  locs.shadowMap0 = GetShaderLocation(target, "shadowMap0");
  locs.shadowMap1 = GetShaderLocation(target, "shadowMap1");

  return uniformCache.emplace(target.id, locs).first->second;
}

void ShadowMap::applyTo(Shader &target) {
  if (target.id == 0) return;

  const UniformLocs &locs = locsFor(target);

  Matrix vp[kCascadeCount];
  float texelWorld[kCascadeCount];
  for (int i = 0; i < kCascadeCount; i++) {
    // With no depth map, hand the shader a transform that lands every fragment
    // outside the [0,1] light-space box. Its out-of-frustum guard then reports
    // "lit" for everything, which is the right failure mode -- leaving lightVP
    // unset would sample an unbound texture, read 0.0, and shadow the whole
    // scene solid black.
    vp[i] = ready ? cascades[i].viewProj : MatrixTranslate(10.0f, 10.0f, 10.0f);
    texelWorld[i] = cascades[i].extent / kResolution;
  }

  if (locs.lightVP != -1) {
    // No SetShaderValueMatrixV in raylib, and the matrices must land in one
    // contiguous mat4[2] upload, so go through rlSetUniformMatrices.
    rlEnableShader(target.id);
    rlSetUniformMatrices(locs.lightVP, vp, kCascadeCount);
  }
  if (locs.texelWorld != -1) {
    SetShaderValueV(target, locs.texelWorld, texelWorld, SHADER_UNIFORM_FLOAT,
                    kCascadeCount);
  }
  if (locs.resolution != -1) {
    int resolution = kResolution; // both cascades share it
    SetShaderValue(target, locs.resolution, &resolution, SHADER_UNIFORM_INT);
  }
  if (locs.lightDir != -1) {
    SetShaderValue(target, locs.lightDir, &lightDirection, SHADER_UNIFORM_VEC3);
  }

  if (!ready) return;

  const int mapLocs[kCascadeCount] = {locs.shadowMap0, locs.shadowMap1};
  rlEnableShader(target.id);
  for (int i = 0; i < kCascadeCount; i++) {
    if (mapLocs[i] == -1) continue;
    int slot = cascades[i].textureSlot;
    rlActiveTextureSlot(slot);
    rlEnableTexture(cascades[i].target.depth.id);
    rlSetUniform(mapLocs[i], &slot, SHADER_UNIFORM_INT, 1);
  }
  // The bindings on slots 14/15 persist; hand the active slot back so nothing
  // downstream binds its diffuse map into a shadow map's unit.
  rlActiveTextureSlot(0);
}
