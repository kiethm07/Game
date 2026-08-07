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
///   * cascade 1 (far)  -- static, framed on the loaded level's bounds by
///     setBounds(). What keeps distant shadows alive, and stable: a frustum
///     that does not move cannot shimmer.
///
/// The near cascade needs no lateral margin for casters just outside it: under
/// an *orthographic* light projection a caster and its shadow share the same
/// light-space XY, so anything whose shadow lands inside the box is inside the
/// box. Casters outside cast outside, which is cascade 1's job.
/// What the depth pass is allowed to render this frame. A debug affordance, not
/// a quality setting: the point is to be able to A/B the depth pass's cost
/// in-game without a rebuild, which is the only way to tell whether the shadow
/// system is actually what a frame is spending its time on.
enum class ShadowMode {
  Full,     ///< both cascades — the shipping configuration
  NearOnly, ///< cascade 0 only. Isolates the far cascade's share of the cost.
  Off,      ///< no depth pass at all. The scene still *samples* the maps, so
            ///< the difference against Full is the depth pass alone, not the
            ///< fragment-side PCF.
};

/// Per-frame counters for the depth pass. Reset at the top of every
/// renderShadowPass and read by the debug overlay.
///
/// `culled` is the number that matters as a level grows: it is what says
/// whether caster culling is doing anything, and a submitted count that tracks
/// total map size rather than the geometry near the player is the failure this
/// exists to make visible.
struct ShadowStats {
  int castersSubmitted[2]{};
  int castersCulled[2]{};
  int trianglesSubmitted[2]{};

  void reset() { *this = ShadowStats{}; }
};

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

  /// Frame the far cascade over a level. Call once when the level is loaded,
  /// before the first depth pass.
  ///
  /// The far cascade is static by design, so it needs to be told where the
  /// world is; without this it stays on its default 48m box at the origin, and
  /// a level authored anywhere else loses its distant shadows entirely. Pass
  /// bounds covering the visual mesh as well as the collision proxies — a roof
  /// with no proxy still casts.
  void setBounds(BoundingBox bounds);

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

  /// World-space extent of a cascade's ortho box, in metres.
  float getExtent(int cascade) const;

  /// Whether a caster with this world AABB can contribute to `cascade`.
  ///
  /// Tests the box against the cascade's ortho bounds in light space, on X and
  /// Y **only** — never along the light. A caster behind the slab's near plane
  /// still casts into the frustum, so a depth test here would delete shadows
  /// that should exist. The slab is 120m deep against a 60m light offset, so it
  /// swallows anything plausible anyway and a Z test would buy nothing for that
  /// risk.
  ///
  /// Sound in the other direction too: under an *orthographic* light a caster
  /// and its shadow share the same light-space XY, so a box outside the
  /// cascade's XY bounds cannot land a shadow inside them.
  bool casterVisible(int cascade, BoundingBox worldBox) const;

  /// Per-frame depth-pass counters. Written by GameRenderer as it submits
  /// casters; read by the debug overlay.
  ShadowStats &stats() { return frameStats; }
  const ShadowStats &stats() const { return frameStats; }

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
    /// The light's view matrix on its own. viewProj is only assembled inside
    /// beginDepthPass, from rlgl's live state — too late for culling, which has
    /// to decide what to submit *before* the pass opens.
    Matrix view{};
    float extent = 0.0f;
    int textureSlot = 0;
  };

  const UniformLocs &locsFor(const Shader &target);
  void computeCascade(int index, Vector3 focus);

  Cascade cascades[kCascadeCount]{};
  Shader staticDepthShader{};
  Vector3 lightDirection{};

  /// Where the far cascade is parked. Set by setBounds(); the centre of the
  /// level rather than of the camera, because a static frustum is what keeps
  /// distant shadows from shimmering as the player walks.
  Vector3 farCentre{};

  bool ready = false;

  ShadowStats frameStats{};

  std::unordered_map<unsigned int, UniformLocs> uniformCache;
};
