#pragma once

#include <raylib.h>

/// Pure utility functions for skeletal animation with raylib.
/// These are stateless helpers with no dependency on any game class.
namespace AnimUtils {

/// Keyframes-per-second the exported clips are sampled at. Playback time in
/// seconds is multiplied by this to derive the (fractional) keyframe to
/// display. Must match raylib's GLTF_FRAMERATE: the glTF loader resamples every
/// clip to that rate on load, so the rate the clip was authored at is irrelevant.
constexpr float ANIM_SAMPLE_RATE = 60.0f;

/// How far bone 0 must get from its starting point, in model-space units,
/// before a clip counts as having root motion. Below this a clip is "in place"
/// and its bone 0 movement is pose detail (hip sway, weight shifts) that must
/// NOT be cancelled. Sits between the ~0.10 of an idle's sway and the ~1.38 of
/// a walk stride; retune if a clip lands near the boundary.
constexpr float ROOT_MOTION_MIN_TRAVEL = 0.25f;

/// Returns whether the clip's bone 0 actually travels, as opposed to swaying
/// in place.
///
/// Classified by peak excursion from the first keyframe, not by net first-to-
/// last displacement: looping clips close the loop by returning the root to
/// its origin, so a walk that strides 1.4m forward still ends ~0 from where it
/// began and would read as in-place. Amplitude is what separates a stride from
/// a weight shift.
///
/// @param anim      The clip to classify.
/// @param minTravel Peak XZ distance required to count as root motion.
/// @return          True if the clip carries root motion worth cancelling.
bool hasRootMotion(const ModelAnimation &anim,
                   float minTravel = ROOT_MOTION_MIN_TRAVEL);

/// Returns the corrected draw position for an entity whose animation contains
/// root motion baked into bone 0.
///
/// In-place clips are returned unchanged (see hasRootMotion), so this is safe
/// to call for every clip.
///
/// Reads the local XZ delta of bone 0 between frame 0 and the current frame,
/// rotates it into world space by the character's yaw, and subtracts it from
/// entityPos so the entity stays anchored to its physics position while the
/// skeleton animates forward.
///
/// @param anim       The active ModelAnimation.
/// @param frame      Current (fractional) keyframe, the same value handed to
///                   UpdateModelAnimation. Bounds-checked internally.
/// @param entityPos  The authoritative physics position of the entity.
/// @param yawDeg     Y-axis rotation of the entity in degrees.
/// @param modelScale Uniform scale factor applied to the model at draw time.
/// @return           Adjusted world-space draw position.
Vector3 cancelRootMotion(const ModelAnimation &anim, float frame,
                         Vector3 entityPos, float yawDeg,
                         float modelScale = 1.0f);

} // namespace AnimUtils
