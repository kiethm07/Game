#pragma once

#include <raylib.h>

/// Pure utility functions for skeletal animation with raylib.
/// These are stateless helpers with no dependency on any game class.
namespace AnimUtils {

/// Keyframes-per-second the exported clips are sampled at. Playback time in
/// seconds is multiplied by this to derive the (fractional) keyframe to display.
/// Kept at 60 to preserve the original tick-locked playback speed.
constexpr float ANIM_SAMPLE_RATE = 60.0f;

/// Returns the corrected draw position for an entity whose animation contains
/// root motion baked into bone 0.
///
/// Reads the local XZ delta of bone 0 between frame 0 and the current frame,
/// rotates it into world space by the character's yaw, and subtracts it from
/// entityPos so the entity stays anchored to its physics position while the
/// skeleton animates forward.
///
/// @param anim       The active ModelAnimation.
/// @param frame      Current keyframe index (bounds-checked internally).
/// @param entityPos  The authoritative physics position of the entity.
/// @param yawDeg     Y-axis rotation of the entity in degrees.
/// @param modelScale Uniform scale factor applied to the model at draw time.
/// @return           Adjusted world-space draw position.
Vector3 cancelRootMotion(const ModelAnimation &anim, int frame,
                         Vector3 entityPos, float yawDeg, float modelScale = 1.0f);

} // namespace AnimUtils
