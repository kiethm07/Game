#pragma once

#include <raylib.h>
#include <unordered_map>

/// Directional-light shadow map, in two cascades.
///
/// One uniform-resolution map cannot serve a camera that orbits 3-4m from the
/// player across a 30m arena: a texel is a fixed size in world space, but a
/// screen pixel is ~1.5mm per metre of view distance, so a map sized for the
/// arena is roughly 5x too coarse in the near field and finer than the screen
/// past ~16m. Hence a split:
///
///   * cascade 0 (near) -- 16m, recentred on the player every frame and snapped
///     to its own texel grid so the edges do not crawl as he walks. ~1.6 screen
///     pixels per texel at the camera's usual distance.
///   * cascade 1 (far)  -- 48m, static, covering the whole arena. Identical to
///     the single map this replaced, and what keeps distant shadows alive.
///
/// The near cascade needs no lateral margin for casters just outside it: under
/// an *orthographic* light projection a caster and its shadow share the same
/// light-space XY, so anything whose shadow lands inside the box is inside the
/// box. Casters outside cast outside, which is cascade 1's job.
class ShadowMap {
public:
  static constexpr int kCascadeCount = 2;

  ShadowMap();
  ~ShadowMap();

  ShadowMap(const ShadowMap &) = delete;
  ShadowMap &operator=(const ShadowMap &) = delete;

  /// False if a framebuffer or the depth shader failed to come up. Callers
  /// should skip the depth pass; applyTo() stays safe to call and leaves the
  /// scene fully lit rather than fully black.
  bool isReady() const { return ready; }

  /// Recentre the near cascade on `focus` (the player). Call once per frame,
  /// before the depth passes.
  void update(Vector3 focus);

  /// Opens the depth pass for one cascade: binds its framebuffer, aims an
  /// orthographic camera down the light, and captures the view-projection the
  /// scene shaders will sample with. Draw casters, then endDepthPass().
  void beginDepthPass(int cascade);
  void endDepthPass();

  /// Pushes both cascades' matrices, texel sizes and light direction into
  /// `target` and binds both depth textures. Call once per frame for every
  /// shadow-receiving shader, before anything is drawn with it.
  void applyTo(Shader &target);

  /// Depth-only shader for static geometry. Bind with BeginShaderMode; it works
  /// for both rlgl immediate mode and DrawMesh (see depth.vs).
  Shader getStaticDepthShader() const { return staticDepthShader; }

  /// Direction the light travels, normalized.
  Vector3 getLightDirection() const { return lightDirection; }

  /// Raw depth attachment for a cascade, for the debug overlay.
  Texture2D getDepthTexture(int cascade) const;

private:
  /// Uniform locations resolved once per shader id. GetShaderLocation is a
  /// glGetUniformLocation round-trip, and applyTo runs every frame.
  struct UniformLocs {
    int lightVP = -1;      ///< mat4[kCascadeCount]
    int texelWorld = -1;   ///< float[kCascadeCount], metres per texel
    int resolution = -1;   ///< int, shared by both cascades
    int lightDir = -1;
    int shadowMap0 = -1;
    int shadowMap1 = -1;
  };

  struct Cascade {
    RenderTexture2D target{};
    Camera3D camera{};
    Matrix projection{};
    Matrix viewProj{};
    float extent = 0.0f;
    int textureSlot = 0;
  };

  const UniformLocs &locsFor(const Shader &target);
  void computeCascade(int index, Vector3 focus);

  Cascade cascades[kCascadeCount]{};
  Shader staticDepthShader{};
  Vector3 lightDirection{};

  bool ready = false;

  std::unordered_map<unsigned int, UniformLocs> uniformCache;
};
