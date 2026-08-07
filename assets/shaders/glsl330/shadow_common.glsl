// Cascaded shadow-map sampling, shared by every shader that receives shadows:
// world.fs (obstacles), level.fs (the level mesh) and skinning.fs (characters).
//
// This used to be copy-pasted into each of them, because GLSL 330 has no
// #include -- with a warning comment on both copies asking whoever edited one
// to remember the other. ShaderLibrary::load splices this file in at load time
// instead, so there is now one copy and they cannot drift.
//
// Include it AFTER the `#version` line and before main(). It declares its own
// uniforms; ShadowMap::applyTo is what fills them in.

uniform vec3 lightDir;   ///< direction the light travels, normalized
uniform mat4 lightVP[2];             ///< light view*projection, per cascade
uniform float shadowTexelWorld[2];   ///< metres covered by one texel, per cascade
uniform int shadowMapResolution;     ///< shared by both cascades
uniform sampler2D shadowMap0;        ///< near cascade, follows the player
uniform sampler2D shadowMap1;        ///< far cascade, framed on the level bounds

// Bias is expressed in world units and in multiples of a texel, then converted
// to depth units once, rather than being hand-tuned depth constants. The two
// cascades have texels 3x apart in size, so a fixed depth bias correct for one
// is wrong for the other; deriving it from shadowTexelWorld makes the whole
// system independent of extent and resolution.
const float SHADOW_DEPTH_RANGE = 120.0;    // kFarPlane - kNearPlane in ShadowMap.cpp
const float SHADOW_NORMAL_TEXELS = 2.0;    // normal-offset, in texels, at grazing
const float SHADOW_SLOPE_TEXELS = 1.0;     // depth bias per texel of depth slope
const float SHADOW_MIN_WORLD = 0.005;      // 5mm floor

// How far past edge-on a surface has to turn before the depth map is trusted at
// full weight. Narrow on purpose: every box side face in the level sits at
// |N.L| >= 0.29, so this only ever engages on curved geometry and on ramps
// whose slope happens to run near-perpendicular to the light.
const float SHADOW_TERMINATOR = 0.2;

/// Where the near cascade starts handing over to the far one, as a fraction of
/// its half-extent. The two have different texel sizes, so a hard switch shows
/// as a seam; this fades across the outer 15%.
const float SHADOW_BLEND_START = 0.85;

/// One bilinear PCF tap: four depth fetches, each compared separately and then
/// weighted by the sub-texel position.
///
/// This is the whole point of the pass. A plain texture() fetch against a
/// GL_NEAREST depth map makes every tap a binary compare at a texel centre, so
/// a 3x3 kernel could only ever produce ten distinct values and each texel
/// rendered as a flat plateau. That quantisation -- not the texel size on its
/// own -- is what read as "pixelated". Comparing first and interpolating after
/// is what a hardware comparison sampler does; raylib's rlLoadTextureDepth
/// hardcodes GL_NEAREST and exposes no way to turn it on, so do it by hand.
float shadowTap(sampler2D map, vec2 uv, float ref, float res)
{
    vec2 t = uv*res - 0.5;
    vec2 base = floor(t);
    vec2 f = t - base;
    vec2 uv00 = (base + 0.5)/res;
    float inv = 1.0/res;

    float s00 = step(texture(map, uv00).r, ref);
    float s10 = step(texture(map, uv00 + vec2(inv, 0.0)).r, ref);
    float s01 = step(texture(map, uv00 + vec2(0.0, inv)).r, ref);
    float s11 = step(texture(map, uv00 + vec2(inv, inv)).r, ref);

    return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
}

/// Four bilinear taps at +-0.5 texel. Same ~3-texel penumbra the old 3x3 binary
/// kernel had, 16 fetches instead of 9, but a continuous gradient.
float shadowPcf(sampler2D map, vec2 uv, float ref, float res)
{
    float o = 0.5/res;
    return 0.25*(shadowTap(map, uv + vec2(-o, -o), ref, res) +
                 shadowTap(map, uv + vec2( o, -o), ref, res) +
                 shadowTap(map, uv + vec2(-o,  o), ref, res) +
                 shadowTap(map, uv + vec2( o,  o), ref, res));
}

/// World position -> [0,1] light-space coordinates for one cascade, with the
/// normal-offset bias applied.
///
/// The offset is scaled by the cascade's texel size and by sin(angle to the
/// light): full strength at grazing incidence where acne lives, near zero on
/// surfaces facing the light head-on, where offsetting would only walk the
/// shadow away from its contact point. Orthographic projections always yield
/// w = 1, so no perspective divide is needed.
vec3 shadowProject(int cascade, vec3 worldPos, vec3 normal, float slope)
{
    vec3 offset = normal*(SHADOW_NORMAL_TEXELS*shadowTexelWorld[cascade]*slope);
    vec4 p = lightVP[cascade]*vec4(worldPos + offset, 1.0);
    return (p.xyz/p.w)*0.5 + 0.5;
}

/// Depth-compare bias for one cascade, from the depth slope across one texel.
float shadowBias(int cascade, float tanTheta)
{
    float world = SHADOW_SLOPE_TEXELS*shadowTexelWorld[cascade]*tanTheta
                + SHADOW_MIN_WORLD;
    return world/SHADOW_DEPTH_RANGE;
}

/// 0.0 fully lit .. 1.0 fully shadowed. `lightVec` points *towards* the light.
float shadowFactor(vec3 worldPos, vec3 normal, vec3 lightVec)
{
    float NdotL = dot(normal, lightVec);

    // A surface turned away from the light is shadowed by its own geometry, and
    // the depth map has nothing to add. Asking it anyway put a light band along
    // every obstacle edge: the normal offset pushes the lookup outwards, and
    // within a few centimetres of a silhouette it lands off the caster's own
    // footprint, on a texel holding the distant ground. The map then answers
    // "lit" and the face jumps from 0.37 to 0.60.
    //
    // Ramp in across SHADOW_TERMINATOR rather than cutting at zero, so this
    // does not just trade the band for a hard seam along the terminator.
    float facing = smoothstep(0.0, SHADOW_TERMINATOR, NdotL);
    if (facing <= 0.0) return 1.0;

    float slope = sqrt(max(1.0 - NdotL*NdotL, 0.0));
    float tanTheta = slope/max(NdotL, 0.15);
    float res = float(shadowMapResolution);

    // Cascade 0 first: it is three times finer and covers everything the camera
    // is close to, which is the only place the coarse map was ever visible.
    vec3 p0 = shadowProject(0, worldPos, normal, slope);
    float edge = max(abs(p0.x - 0.5), abs(p0.y - 0.5))*2.0;
    float w = 1.0 - smoothstep(SHADOW_BLEND_START, 1.0, edge);
    if (p0.z > 1.0) w = 0.0;

    float shadow;
    if (w >= 0.999)
    {
        shadow = shadowPcf(shadowMap0, p0.xy, p0.z - shadowBias(0, tanTheta), res);
    }
    else
    {
        vec3 p1 = shadowProject(1, worldPos, normal, slope);

        // Outside the far cascade there is no depth to compare against. The
        // cascade is framed on the level's own bounds, so this is the normal
        // case only past the edge of the map: treat it as lit, never shadowed.
        float farShadow = 0.0;
        if (p1.z <= 1.0 &&
            all(greaterThanEqual(p1.xy, vec2(0.0))) &&
            all(lessThanEqual(p1.xy, vec2(1.0))))
        {
            farShadow = shadowPcf(shadowMap1, p1.xy, p1.z - shadowBias(1, tanTheta), res);
        }

        shadow = (w <= 0.001)
               ? farShadow
               : mix(farShadow,
                     shadowPcf(shadowMap0, p0.xy, p0.z - shadowBias(0, tanTheta), res),
                     w);
    }

    return max(shadow, 1.0 - facing);
}
