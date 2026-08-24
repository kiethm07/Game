#include <Rendering/PostureMeter.h>

#include <algorithm>

namespace {

/// The unfilled part of the meter, and the rim drawn around the whole
/// silhouette. Not in the Style, because nothing wants them per-meter: what
/// distinguishes the player's bar from an enemy's is the fill, and letting the
/// track differ too would only let the three drift apart.
constexpr Color kTrack = {20, 17, 14, 195};
constexpr Color kRim = {236, 226, 200, 205};

/// The meter's silhouette clipped to `half_extent` either side of the centre.
///
/// The empty track and the fill are this same shape at two different extents,
/// which is what makes a fill follow the taper for free: short of the caps it
/// is a plain rectangle, and at 100% it is the outline exactly.
///
/// Winding matters here. raylib enables backface culling at init and never
/// turns it off for the 2D pass, and its 2D projection flips Y -- so a triangle
/// wound clockwise *as it appears on screen* is a back face and draws nothing
/// at all. Every triangle below is wound counter-clockwise on screen.
void drawSpan(float cx, float cy, float half_width, float cap, float height,
              float half_extent, Color color) {
  if (half_extent <= 0.0f) return;

  const float hh = height * 0.5f;
  const float shoulder = half_width - cap; // Where the taper starts.

  // The straight middle section, and the whole of the meter until the fill
  // reaches the shoulders.
  const float rect = std::min(half_extent, shoulder);
  if (rect > 0.0f) {
    DrawRectangleRec({cx - rect, cy - hh, rect * 2.0f, height}, color);
  }
  if (half_extent <= shoulder) return;

  // Past the shoulder the edges converge on the tips, so each end is a
  // trapezoid whose outer edge is however tall the taper still is at that x --
  // zero at the tip itself, which is what brings a full meter to a point.
  const float outer = hh * (half_width - half_extent) / cap;

  DrawTriangle({cx + half_extent, cy - outer}, {cx + shoulder, cy - hh},
               {cx + shoulder, cy + hh}, color);
  DrawTriangle({cx + half_extent, cy - outer}, {cx + shoulder, cy + hh},
               {cx + half_extent, cy + outer}, color);

  DrawTriangle({cx - half_extent, cy + outer}, {cx - shoulder, cy + hh},
               {cx - shoulder, cy - hh}, color);
  DrawTriangle({cx - half_extent, cy + outer}, {cx - shoulder, cy - hh},
               {cx - half_extent, cy - outer}, color);
}

/// Trace the six corners of the silhouette. DrawLineEx builds its quad from the
/// segment's own direction, so unlike the triangles above these do not care
/// which way round the loop is walked.
void drawRim(float cx, float cy, float half_width, float cap, float height,
             float thickness) {
  const float hh = height * 0.5f;
  const float shoulder = half_width - cap;
  const Vector2 corners[6] = {
      {cx + half_width, cy}, {cx + shoulder, cy - hh}, {cx - shoulder, cy - hh},
      {cx - half_width, cy}, {cx - shoulder, cy + hh}, {cx + shoulder, cy + hh}};

  for (int i = 0; i < 6; i++) {
    DrawLineEx(corners[i], corners[(i + 1) % 6], thickness, kRim);
  }
}

} // namespace

void PostureMeter::draw(float cx, float cy, float fill, const Style &style) {
  const float half_width = std::max(style.half_width, 1.0f);
  const float height = std::max(style.height, 1.0f);

  // A cap longer than the meter is half wide would fold the shoulders past each
  // other and turn the trapezoids inside out. Clamped, the worst a caller gets
  // is a plain diamond.
  const float cap = std::min(std::max(style.cap, 1.0f), half_width);
  const float pct = std::min(std::max(fill, 0.0f), 1.0f);

  // Track, then fill, then the rim over both -- so the rim reads as the edge of
  // the meter rather than as something the fill is painted over.
  drawSpan(cx, cy, half_width, cap, height, half_width, kTrack);
  drawSpan(cx, cy, half_width, cap, height, half_width * pct, style.fill);
  drawRim(cx, cy, half_width, cap, height, 1.5f);
}
