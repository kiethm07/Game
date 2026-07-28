#pragma once

/// Pure utility constants for skeletal animation with raylib.
/// Stateless, with no dependency on any game class.
namespace AnimUtils {

/// Keyframes-per-second the exported clips are sampled at. Playback time in
/// seconds is multiplied by this to derive the (fractional) keyframe to
/// display. Must match raylib's GLTF_FRAMERATE: the glTF loader resamples every
/// clip to that rate on load, so the rate the clip was authored at is irrelevant.
constexpr float ANIM_SAMPLE_RATE = 60.0f;

} // namespace AnimUtils
