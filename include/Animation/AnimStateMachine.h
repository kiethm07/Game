#pragma once

#include <Components/AnimationComponent.h>
#include <Rendering/AssetID.h>
#include <Rendering/AssetManager.h>
#include <Rendering/RenderData.h>
#include <Rendering/RootMotion.h>

/// Clip catalogue and playback driver for one entity.
///
/// Owns the three things every animated entity needs and none of them
/// gameplay-specific: the table describing each state's clip, the name-to-index
/// resolution that table needs, and the playback clock those indices drive.
/// What is left to the owner is the only genuinely per-entity part — deciding
/// which state is current, which is a question about gameplay, not about
/// animation.
///
/// `StateEnum` is a plain enum class whose last enumerator is `Count`. The table
/// is indexed by it, so a state and its row cannot drift apart.
template <typename StateEnum> class AnimStateMachine {
public:
  /// Everything about a state that does not depend on the current frame. One
  /// row per state, so adding a state is a table entry plus whatever the owner
  /// does to select it.
  struct Desc {
    /// Looked up by name because clip indices shift with the export toolchain
    /// (see AssetManager::findAnimation). nullptr for states with no authored
    /// animation yet, which leaves their index at -1.
    const char *clip;
    bool loop;
    float rate;
    bool rootDriven;

    /// Seconds spent cross-fading into this state. Per state rather than per
    /// transition because what governs the length is the state being entered:
    /// a swing or a flinch has to land now, a stride can afford to ease.
    float fadeIn;

    /// Seconds to skip at the head of the clip. Zero for all but clips authored
    /// with a lead-in the engine has already played out for real — a landing
    /// that opens with its own descent, entered at the frame the feet land.
    float startAt = 0.0f;
  };

  /// What the owner decided this frame.
  struct Selection {
    StateEnum state{};

    /// Distinguishes re-entering a state from staying in it, for the states
    /// where that difference is visible: a fresh swing and a fresh flinch both
    /// reuse the clip they are already playing, and play() is a no-op on the
    /// current clip. Zero for states that cannot meaningfully re-trigger.
    unsigned variant = 0;

    int clip = -1;
    bool rootDriven = false;

    /// Playback rate, seeded from the row and overridable per frame for the
    /// states whose pace is not a property of the clip alone — a locomotion
    /// cycle time-scaled to whatever speed the character is actually
    /// travelling at.
    float rate = 1.0f;
  };

  static constexpr int STATE_COUNT = static_cast<int>(StateEnum::Count);

  AnimStateMachine(AssetID asset, const Desc *table) : asset(asset), table(table) {
    for (int i = 0; i < STATE_COUNT; ++i)
      clip_index[i] = -1;
  }

  /// Resolves every row's clip name to an index. Returns true on the one frame
  /// it does the work, so an owner with something to seed from the result — a
  /// locomotion speed taken from the run clip — can do it without a flag of its
  /// own.
  bool resolveClips(const AssetManager &assets) {
    if (clips_resolved)
      return false;
    clips_resolved = true;

    for (int i = 0; i < STATE_COUNT; ++i) {
      const Desc &d = table[i];
      clip_index[i] = d.clip ? assets.findAnimation(asset, d.clip) : -1;
    }
    return true;
  }

  /// Clip index for a state, or -1 when the row names no clip or the loaded
  /// asset does not contain it. Selection ladders guard on this so a missing
  /// animation falls through to the next candidate rather than freezing the
  /// character.
  int clipFor(StateEnum state) const { return clip_index[index(state)]; }

  /// Overrides a resolved index. For assets whose clip is not named the way the
  /// table expects but whose contents are known anyway — a single-clip model
  /// where index 0 is the only possibility.
  void setClip(StateEnum state, int clip) { clip_index[index(state)] = clip; }

  const Desc &desc(StateEnum state) const { return table[index(state)]; }

  /// A selection filled from the table. Callers may overwrite `clip`,
  /// `rootDriven` and `rate` afterwards, which is how a state whose clip varies
  /// per action — an attack naming its own swing — overrides its row.
  Selection select(StateEnum state, unsigned variant = 0) const {
    return Selection{state, variant, clipFor(state), desc(state).rootDriven,
                     desc(state).rate};
  }

  const RootMotion::Track &track(const AssetManager &assets,
                                 StateEnum state) const {
    return assets.getRootMotion(asset, clipFor(state));
  }

  /// Drives the clock from a selection, and returns the root motion track of
  /// the clip that ended up playing. Safe to call every frame with the same
  /// selection; that is the steady state.
  const RootMotion::Track &apply(const AssetManager *assets, float dt,
                                 const Selection &selection) {
    const Desc &d = desc(selection.state);

    if (selection.clip >= 0) {
      // Read before play(), which is what changes it. A switch already starts
      // its own cross-fade, so the restart below must not fire on top and
      // replace it with a degenerate frame-0-to-frame-0 fade.
      const bool switched = (selection.clip != animation.index());

      animation.play(selection.clip, d.loop, d.fadeIn, d.startAt);

      // Re-entering a state the clip is already playing: a combo step that
      // reuses the previous step's swing, or a second flinch during one guard.
      // play() is a no-op on the current clip, so those need an explicit rewind
      // to read as separate actions rather than one held pose — cross-fading
      // out of the pose being held, so the rewind eases rather than snaps.
      const bool reentered = (selection.state != prev.state ||
                              selection.variant != prev.variant);
      if (!switched && reentered)
        animation.restart(d.fadeIn, d.startAt);
    }

    // Set after play(), so the fade snapshot above captures the outgoing
    // state's rate — a run fading out has to keep striding at its own rate, not
    // drop to the incoming state's mid-fade. The selection's rate rather than
    // the row's, so an owner that time-scales a cycle to the speed it is
    // travelling at is obeyed; select() seeds it from the row, so a state that
    // does not is unaffected.
    animation.setPlaybackRate(selection.rate);

    const RootMotion::Track &t =
        assets ? assets->getRootMotion(asset, animation.index()) : kNoTrack;
    animation.advance(dt, t.duration);

    prev = selection;
    return t;
  }

  /// This frame's root translation, in the model's local space. The owner
  /// rotates it into the world and decides what to do with it — keeping
  /// playback time inside the animation layer.
  Vector3 sampleRootDelta(const RootMotion::Track &t) const {
    return RootMotion::sampleDelta(t, animation.prevFrame(), animation.frame());
  }

  AnimationState renderState() const {
    AnimationState state;
    state.animIndex = animation.index();
    state.animTime = animation.time();
    state.blendFromIndex = animation.fadeFromIndex();
    state.blendFromTime = animation.fadeFromTime();
    state.blend = animation.blend();
    return state;
  }

private:
  static int index(StateEnum state) { return static_cast<int>(state); }

  /// Returned when there is no asset manager to ask for a track. Static so
  /// callers can hold a reference to it.
  static inline const RootMotion::Track kNoTrack{};

  AssetID asset;
  const Desc *table;

  AnimationComponent animation;

  /// Clip indices per state, resolved by name on the first resolveClips(). -1
  /// until then, and for clips the loaded asset does not contain.
  int clip_index[STATE_COUNT];
  bool clips_resolved = false;

  /// Last frame's selection, compared against this frame's to tell re-entering
  /// a state from staying in it.
  Selection prev;
};
