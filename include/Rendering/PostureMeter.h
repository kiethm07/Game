#pragma once

#include <raylib.h>

/// The tapered posture meter, shared by the player, ordinary enemies and the
/// two bosses.
///
/// One routine rather than three copies, because the three differ only in where
/// they sit, how wide they are and what colour they fill. The geometry -- a
/// straight middle section with a triangular cap at either end, filling
/// symmetrically out from the centre -- is the same meter in all three places,
/// and drawn once it cannot drift between them.
namespace PostureMeter {

/// Fill colours. The player's yellow and the enemy's hot orange differ on
/// purpose: both meters are on screen during a fight, and the one at the centre
/// has to read as *yours* without a label to say so.
inline constexpr Color kPlayerFill = {255, 208, 64, 255};
inline constexpr Color kEnemyFill = {255, 104, 24, 255};

/// How one meter is laid out. Every length is in pixels.
struct Style {
  /// Centre to either tip, so the meter is twice this wide overall.
  float half_width = 100.0f;

  /// Thickness of the straight middle section.
  float height = 10.0f;

  /// How much of each end is taper.
  float cap = 20.0f;

  /// Colour of the filled part. The empty track and the rim are fixed.
  Color fill = kPlayerFill;
};

/// Draw one meter centred on (cx, cy).
///
/// `cy` is the vertical middle of the bar rather than its top edge, because the
/// shape is symmetric about it and a caller placing one by its top would have
/// to subtract half the height every time.
///
/// `fill` is 0..1 and is clamped. A posture percentage is already in range, but
/// clamping is what stops a caller doing its own scaling from pushing a vertex
/// outside the silhouette the rim is drawn around.
void draw(float cx, float cy, float fill, const Style &style);

} // namespace PostureMeter
