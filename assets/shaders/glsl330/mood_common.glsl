// The scene's mood, in one place: how bright a lit surface is, how cold the
// shade reads, and how fast distance dissolves into mist.
//
// Spliced into world.fs (obstacles), level.fs (the level mesh), grass.fs and
// skinning.fs (characters) by ShaderLibrary::load, the same way
// shadow_common.glsl is. That splice is the whole reason this file exists:
// these constants used to be copy-pasted into three shaders with a comment on
// each asking whoever edited one to remember the others, and the grass, the
// ground and a wall standing on it have to agree exactly or the greybox stops
// matching the art.
//
// Include it AFTER the `#version` line and before main().
//
// The look being aimed at is an overcast forest floor -- no visible sun, light
// arriving from a bright grey sky, everything past a few dozen metres eaten by
// mist. Three things do that work, and each is doing a specific job:
//
//   * a LOW key against a not-much-lower ambient. Overcast light is nearly
//     shadowless; a big key/ambient ratio is what reads as noon sunshine.
//   * a COOL, slightly green ambient tint. Shade under a canopy is lit by the
//     sky and by bounce off leaves, so it is never neutral grey. Tinting the
//     ambient rather than the final colour keeps the key light honest and stops
//     the whole frame turning green.
//   * DISTANCE FOG toward the sky colour, which is also what the frame is
//     cleared to, so geometry dissolves into the background instead of ending
//     against it.

// ---------------------------------------------------------------------------
// Environment: obstacles, the level mesh, the grass.
//
// Down from AMBIENT 0.60 / KEY 0.55. Lit ground used to land near 1.06 -- above
// white, which is what made the map read as midday. It now sits near 0.60, and
// shadowed ground near 0.26, so the range lives in the lower half where an
// overcast scene belongs.
// ---------------------------------------------------------------------------
const float MOOD_AMBIENT = 0.34;
const float MOOD_KEY = 0.26;

/// How far the ambient floor drops in shadow. Higher than the 0.62 it replaces:
/// with ambient this low, dimming it as hard as before crushed shadowed ground
/// to near black and lost the terrain's own shading inside it.
const float MOOD_SHADOW_AMBIENT_SCALE = 0.76;

/// Colour of the ambient term. Cool and faintly green -- sky light plus bounce
/// off a canopy. Deliberately close to 1.0 on green so this shifts the hue
/// without darkening the scene a second time.
const vec3 MOOD_AMBIENT_TINT = vec3(0.78, 0.94, 0.90);

/// Colour of the key term. Also cool, but much closer to neutral: this is the
/// sky read as a broad source, not a warm sun.
const vec3 MOOD_KEY_TINT = vec3(0.90, 0.96, 1.00);

// ---------------------------------------------------------------------------
// Characters.
//
// Kept separate from the environment on purpose. The armour albedos are dark
// steel and go muddy under environment-strength light, so the character rig has
// always run a higher ambient and a midtone lift. Both come down here, but not
// as far -- the point is a character who reads against the mist, not one who
// disappears into it.
// ---------------------------------------------------------------------------
const float MOOD_CHAR_AMBIENT = 0.62;
const float MOOD_CHAR_KEY = 0.34;
const float MOOD_CHAR_SHADOW_AMBIENT_SCALE = 0.78;

/// Midtone lift on characters, applied after the light term. An exponent below
/// 1 raises the middle of the range and leaves highlights alone. Raised from
/// 0.78 toward 1.0: the old value was fighting a bright scene, and at this
/// ambient it flattened the armour into the fog.
const float MOOD_CHAR_LIFT = 0.90;

// ---------------------------------------------------------------------------
// Fog.
//
// Colour and density come from C++ rather than being constants here, because
// the frame is cleared to the same colour and there must be exactly one source
// of truth for it -- see Rendering/Lighting.h, which GameRenderer pushes into
// every shader that includes this file.
// ---------------------------------------------------------------------------
uniform vec3 fogColor;
uniform float fogDensity;
uniform vec3 camPos;

/// Exponential-squared fog. Squared rather than plain exponential because it
/// stays almost clear up close and then closes in quickly, which is how a bank
/// of mist actually reads; plain exp() puts a visible haze on everything
/// including the character two metres in front of the camera.
///
/// `worldPos` is the fragment's world position, not its view depth, so the fog
/// depends only on where a surface is and not on where the camera is pointing.
/// Swinging the camera past a wall therefore does not change how foggy it is.
vec3 applyFog(vec3 color, vec3 worldPos)
{
    float d = distance(worldPos, camPos)*fogDensity;
    float f = 1.0 - exp(-d*d);
    return mix(color, fogColor, clamp(f, 0.0, 1.0));
}
