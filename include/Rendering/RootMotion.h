#pragma once

#include <raylib.h>
#include <vector>

/// Root motion extracted from a clip, precomputed once at load time.
///
/// The GLBs are baked by tools/bake_root_motion.py so that bone 0 is a
/// dedicated `Root` bone carrying only horizontal travel — no hip sway, no
/// vertical bob. That makes extraction exact: there is no magnitude threshold
/// guessing whether a clip strides or merely shifts its weight.
///
/// Two consumers, deliberately separate:
///   * The renderer subtracts sampleOffset() from the draw position so the mesh
///     stays pinned to the physics capsule.
///   * Gameplay reads sampleDelta() (one-shot clips) or authoredSpeed (looping
///     clips) to drive the character controller.
/// Running only the first half is what produced the original foot sliding.
namespace RootMotion {

/// Per-clip root translation track, in the model's local space.
struct Track {
  /// Root offset from keyframe 0, one entry per keyframe. offsets[0] is zero.
  std::vector<Vector3> offsets;

  /// Travel across the playable range — one stride for a locomotion cycle.
  Vector3 totalDelta = {0.0f, 0.0f, 0.0f};

  /// Playable clip length in seconds. One frame shorter than the authored
  /// length, because raylib mis-samples every clip's final keyframe (see
  /// buildTrack). Playback clamps and loops here, so that keyframe is simply
  /// never reached and everything downstream stays self-consistent.
  float duration = 0.0f;

  /// |totalDelta| / duration. The speed the clip was animated to move at; a
  /// controller running at this speed plants its feet without sliding.
  float authoredSpeed = 0.0f;

  /// False for in-place clips (idles, in-place turns), where the root never
  /// leaves the origin and there is nothing to cancel or consume.
  bool hasMotion = false;
};

/// Extracts the root track from a loaded clip. Call once per clip at load.
///
/// Reads bone 0 of anim.keyframePoses, which raylib has already accumulated
/// into model space via BuildPoseFromParentJoints, so the values are the root's
/// position rather than a local TRS. Only XZ is captured: vertical stays with
/// the skeleton so the engine keeps authority over jump arcs.
Track buildTrack(const ModelAnimation &anim);

/// Root offset from keyframe 0 at a (fractional) frame.
///
/// Uses the same lerp-and-wrap index math as UpdateModelAnimation, so the
/// offset and the pose it compensates stay in lockstep — including across the
/// loop point, where raylib blends the last keyframe back toward the first.
Vector3 sampleOffset(const Track &track, float frame);

/// Root translation between two playback frames, in the model's local space.
///
/// Intended for one-shot clips (attacks, dodges), where playback is monotonic
/// and clamps at the end. Loop crossings are compensated by adding one
/// totalDelta per lap so a wrap reads as forward travel instead of a jump back
/// to the origin; looping locomotion should nonetheless prefer authoredSpeed,
/// which is not subject to the one-frame loop blend.
Vector3 sampleDelta(const Track &track, float prevFrame, float currFrame);

/// Rotates a local-space root delta into world space by a Y-axis facing angle.
Vector3 toWorld(Vector3 localDelta, float yawDeg);

} // namespace RootMotion
