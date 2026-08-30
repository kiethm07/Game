#pragma once

#include <raylib.h>

/// The two mood values C++ has to know about.
///
/// Everything else about the look -- ambient, key, tints, the midtone lift --
/// is a compile-time constant in assets/shaders/glsl330/mood_common.glsl, which
/// is where you tune it. These two are here instead because the fog colour is
/// also what the frame is cleared to: geometry has to dissolve INTO the
/// background rather than end against it, and a sky that disagrees with the fog
/// draws a hard line along every distant silhouette. One value, used in both
/// places, is the only way that cannot come apart.
namespace Lighting {

/// The sky, and what distance fades to. A pale, cool, faintly green grey --
/// bright enough to read as an overcast sky through a canopy, dark enough that
/// the scene in front of it is not silhouetted against white.
///
/// Alpha is 255: this is a clear colour as well as a fog colour.
inline constexpr Color kSky{143, 158, 153, 255};

/// Fog thickness, in inverse metres, for the exp2 falloff in mood_common.glsl:
/// `f = 1 - exp(-(distance*density)^2)`.
///
/// At 0.018 a surface is ~10% fogged at 20 m, half gone by 45 m and all but
/// gone by 90 m. Chosen against the levels rather than by eye: the castle
/// approach is roughly 150 m end to end, so this hides the far edge of the map
/// and the seam where the terrain stops, while leaving the 10 m the combat
/// actually happens in essentially clear.
///
/// Raising this past about 0.03 starts eating enemies before the AI has noticed
/// the player, which reads as unfair rather than atmospheric.
inline constexpr float kFogDensity = 0.018f;

/// The sky as the shaders want it: linear 0-1, no alpha.
inline Vector3 skyAsVec3() {
  return Vector3{kSky.r / 255.0f, kSky.g / 255.0f, kSky.b / 255.0f};
}

} // namespace Lighting
