#pragma once

#include <Rendering/AnimUtils.h>

/// Playback clock for one entity's skeletal animation.
///
/// Holds the previous frame's playback time as well as the current one, which
/// is what root motion extraction needs: the displacement a displacing state
/// contributes this tick is the root's travel between those two samples.
///
/// Also holds the clip being faded out of, so a switch can cross-fade instead
/// of snapping. The blend itself is raylib's — UpdateModelAnimationEx takes two
/// (clip, frame) pairs and a weight — so all this owns is keeping the outgoing
/// clip alive and running the weight down.
///
/// Owning the clock here also keeps it from being forgotten — enemies
/// previously declared animTime but never advanced it, so they rendered frozen
/// on frame 0.
class AnimationComponent {
public:
  /// Switches to a clip, restarting it and cross-fading out of the current one
  /// over `fadeIn` seconds. Re-playing the current clip is a no-op, so callers
  /// can call this unconditionally every frame.
  ///
  /// `startTime` enters the clip partway in, for clips whose opening is a
  /// lead-in the engine has already played out for real — a landing authored
  /// with its own descent, entered at the frame the feet reach the floor.
  void play(int index, bool loop, float fadeIn = 0.0f, float startTime = 0.0f) {
    if (index == anim_index) return;
    beginFade(fadeIn);
    anim_index = index;
    looping = loop;
    anim_time = startTime;
    // Equal, not zero: the first advance() must report the travel of one frame,
    // not of everything the skipped lead-in covered.
    prev_anim_time = startTime;
  }

  /// Rewinds the current clip to frame 0 without changing it, cross-fading out
  /// of the pose it was holding. play() is deliberately a no-op when the
  /// requested clip is already current, so a caller re-triggering the same clip
  /// — a combo step that reuses the previous step's animation — needs this to
  /// explicitly restart the swing instead of leaving it frozen on its last
  /// frame. The fade runs the same clip against itself at two different times,
  /// which UpdateModelAnimationEx handles fine.
  void restart(float fadeIn = 0.0f, float startTime = 0.0f) {
    beginFade(fadeIn);
    anim_time = startTime;
    prev_anim_time = startTime;
  }

  /// Advances playback. Looping clips wrap at `duration`, non-looping clips
  /// stop there and report isFinished().
  ///
  /// `duration` is RootMotion::Track::duration, which is deliberately one frame
  /// shorter than the clip's authored length — wrapping here is what keeps
  /// raylib's mis-sampled final keyframe from ever being displayed.
  void advance(float dt, float duration) {
    // Remembered so a later beginFade() knows how long the clip it is
    // snapshotting runs for, without the caller having to pass it again.
    current_duration = duration;

    prev_anim_time = anim_time;
    anim_time += dt * playback_rate;
    wrapTime(anim_time, duration, looping);

    advanceFade(dt);
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

  /// The clip being faded out of, or -1 when no fade is running.
  int fadeFromIndex() const { return fade_from_index; }
  float fadeFromTime() const { return fade_from_time; }

  /// Weight of the current clip against the outgoing one, matching raylib's
  /// convention: 0 is entirely the outgoing clip, 1 entirely the current one.
  float blend() const {
    if (fade_from_index < 0 || fade_length <= 0.0f) return 1.0f;
    return 1.0f - (fade_remaining / fade_length);
  }

  /// Fractional keyframes, the unit UpdateModelAnimation and RootMotion work in.
  float frame() const { return anim_time * AnimUtils::ANIM_SAMPLE_RATE; }
  float prevFrame() const { return prev_anim_time * AnimUtils::ANIM_SAMPLE_RATE; }
  float fadeFromFrame() const { return fade_from_time * AnimUtils::ANIM_SAMPLE_RATE; }

private:
  /// Snapshots the clip about to be replaced so it can be blended out of.
  /// A zero-length fade, or nothing played yet, leaves the switch a hard cut.
  void beginFade(float fadeIn) {
    if (fadeIn <= 0.0f || anim_index < 0) {
      fade_from_index = -1;
      fade_remaining = 0.0f;
      fade_length = 0.0f;
      return;
    }

    fade_from_index = anim_index;
    fade_from_time = anim_time;
    fade_from_duration = current_duration;
    fade_from_rate = playback_rate;
    fade_from_looping = looping;
    fade_remaining = fadeIn;
    fade_length = fadeIn;
  }

  void advanceFade(float dt) {
    if (fade_from_index < 0) return;

    // The outgoing clip keeps playing, at the rate it was playing at. Freezing
    // it instead would stop the outgoing legs dead and slide them along the
    // ground for the length of the fade — the artifact fading exists to remove.
    fade_from_time += dt * fade_from_rate;
    wrapTime(fade_from_time, fade_from_duration, fade_from_looping);

    fade_remaining -= dt;
    if (fade_remaining <= 0.0f) {
      fade_from_index = -1;
      fade_remaining = 0.0f;
    }
  }

  static void wrapTime(float &time, float duration, bool loop) {
    if (duration <= 0.0f) return;

    if (loop) {
      while (time >= duration) time -= duration;
    } else if (time > duration) {
      time = duration;
    }
  }

  /// -1 until the first play(). Doubles as "nothing to fade out of", and keeps
  /// play()'s already-current early-out from swallowing a first clip that
  /// genuinely lives at index 0.
  int anim_index = -1;
  float anim_time = 0.0f;
  float prev_anim_time = 0.0f;
  bool looping = true;
  float playback_rate = 1.0f;
  float current_duration = 0.0f;

  int fade_from_index = -1;
  float fade_from_time = 0.0f;
  float fade_from_duration = 0.0f;
  float fade_from_rate = 1.0f;
  bool fade_from_looping = true;
  float fade_remaining = 0.0f;
  float fade_length = 0.0f;
};
