#pragma once

#include <Rendering/AnimUtils.h>

/// Playback clock for one entity's skeletal animation.
///
/// Holds the previous frame's playback time as well as the current one, which
/// is what root motion extraction needs: the displacement a displacing state
/// contributes this tick is the root's travel between those two samples.
///
/// Owning the clock here also keeps it from being forgotten — enemies
/// previously declared animTime but never advanced it, so they rendered frozen
/// on frame 0.
class AnimationComponent {
public:
  /// Switches to a clip, restarting it. Re-playing the current clip is a no-op,
  /// so callers can call this unconditionally every frame.
  void play(int index, bool loop) {
    if (index == anim_index) return;
    anim_index = index;
    looping = loop;
    anim_time = 0.0f;
    prev_anim_time = 0.0f;
  }

  /// Advances playback. Looping clips wrap at `duration`, non-looping clips
  /// stop there and report isFinished().
  ///
  /// `duration` is RootMotion::Track::duration, which is deliberately one frame
  /// shorter than the clip's authored length — wrapping here is what keeps
  /// raylib's mis-sampled final keyframe from ever being displayed.
  void advance(float dt, float duration) {
    prev_anim_time = anim_time;
    anim_time += dt * playback_rate;

    if (duration <= 0.0f) return;

    if (looping) {
      while (anim_time >= duration) anim_time -= duration;
    } else if (anim_time > duration) {
      anim_time = duration;
    }
  }

  /// Scales playback speed. Used to keep a clip's stride matched to a
  /// controller running faster or slower than the clip was authored for.
  void setPlaybackRate(float rate) { playback_rate = rate; }

  bool isFinished(float duration) const {
    return !looping && duration > 0.0f && anim_time >= duration;
  }

  int index() const { return anim_index; }
  float time() const { return anim_time; }
  bool isLooping() const { return looping; }

  /// Fractional keyframes, the unit UpdateModelAnimation and RootMotion work in.
  float frame() const { return anim_time * AnimUtils::ANIM_SAMPLE_RATE; }
  float prevFrame() const { return prev_anim_time * AnimUtils::ANIM_SAMPLE_RATE; }

private:
  int anim_index = 0;
  float anim_time = 0.0f;
  float prev_anim_time = 0.0f;
  bool looping = true;
  float playback_rate = 1.0f;
};
