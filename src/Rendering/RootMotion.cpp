#include <Rendering/AnimUtils.h>
#include <Rendering/RootMotion.h>
#include <cmath>
#include <raymath.h>

namespace RootMotion {

namespace {
/// Travel below this is float noise in an in-place clip, not locomotion.
constexpr float MIN_TRAVEL = 1e-4f;

/// Wraps a keyframe index into [0, count), matching UpdateModelAnimation's
/// modulo while also tolerating negatives.
int wrapIndex(int index, int count) {
  index %= count;
  return (index < 0) ? index + count : index;
}
} // namespace

Track buildTrack(const ModelAnimation &anim) {
  Track track;
  if (anim.boneCount <= 0 || anim.keyframeCount <= 0 ||
      anim.keyframePoses == nullptr) {
    return track;
  }

  const Vector3 origin = anim.keyframePoses[0][0].translation;

  track.offsets.reserve(static_cast<size_t>(anim.keyframeCount));
  for (int i = 0; i < anim.keyframeCount; i++) {
    const Vector3 pose = anim.keyframePoses[i][0].translation;
    // XZ only. Vertical root travel would fight PhysicsManager's gravity
    // integration, and cancelling it at draw time would stop Jump ever
    // visually leaving the ground.
    track.offsets.push_back({pose.x - origin.x, 0.0f, pose.z - origin.z});
  }

  // The last keyframe is unusable, so the playable range ends one frame early.
  //
  // raylib resamples clips at j/60, which makes the final sample land exactly
  // on the clip's end time — and GetPoseAtTimeGLTF's search loop tests
  // `time < tend`, which no interval satisfies there. It leaves `keyframe` at
  // its default 0 and returns the SECOND keyframe's value
  // (rmodels.c:6357-6371). Read naively, a run cycle's travel comes out as
  // 0.057 instead of 2.506 and locomotion ends up ~46x too slow.
  //
  // Rather than reconstruct that keyframe, treat it as outside the clip:
  // playback clamps and loops at `duration`, so nothing ever samples it. That
  // keeps `offsets` a faithful mirror of keyframePoses — which matters,
  // because sampleOffset feeds the draw cancel and has to agree with whatever
  // pose UpdateModelAnimation actually produces. Discarding one frame at 60 Hz
  // is imperceptible; a mismatched cancel would throw the mesh a full stride
  // off its capsule.
  const int lastUsable = anim.keyframeCount - 2;
  if (lastUsable < 1) {
    // A clip too short to have a usable range at all (2 keyframes or fewer).
    track.duration = 0.0f;
    return track;
  }

  track.duration = static_cast<float>(lastUsable) / AnimUtils::ANIM_SAMPLE_RATE;
  track.totalDelta = track.offsets[static_cast<size_t>(lastUsable)];

  const float travel = Vector3Length(track.totalDelta);
  track.hasMotion = travel > MIN_TRAVEL;
  if (track.hasMotion && track.duration > 0.0f) {
    track.authoredSpeed = travel / track.duration;
  }

  return track;
}

Vector3 sampleOffset(const Track &track, float frame) {
  const int count = static_cast<int>(track.offsets.size());
  if (count == 0) return {0.0f, 0.0f, 0.0f};

  const int current = static_cast<int>(frame);
  const float blend = Clamp(frame - static_cast<float>(current), 0.0f, 1.0f);

  return Vector3Lerp(track.offsets[wrapIndex(current, count)],
                     track.offsets[wrapIndex(current + 1, count)], blend);
}

Vector3 sampleDelta(const Track &track, float prevFrame, float currFrame) {
  const int count = static_cast<int>(track.offsets.size());
  if (count == 0) return {0.0f, 0.0f, 0.0f};

  Vector3 delta = Vector3Subtract(sampleOffset(track, currFrame),
                                  sampleOffset(track, prevFrame));

  // Each loop boundary crossed resets the sampled offset to zero; add back the
  // stride it represents so the caller sees continuous forward travel.
  const float span = static_cast<float>(count);
  const int laps = static_cast<int>(std::floor(currFrame / span)) -
                   static_cast<int>(std::floor(prevFrame / span));
  if (laps != 0) {
    delta = Vector3Add(delta, Vector3Scale(track.totalDelta,
                                           static_cast<float>(laps)));
  }

  return delta;
}

Vector3 toWorld(Vector3 localDelta, float yawDeg) {
  const float yaw = yawDeg * DEG2RAD;
  const float sinYaw = sinf(yaw);
  const float cosYaw = cosf(yaw);
  return {localDelta.x * cosYaw + localDelta.z * sinYaw, localDelta.y,
          -localDelta.x * sinYaw + localDelta.z * cosYaw};
}

} // namespace RootMotion
