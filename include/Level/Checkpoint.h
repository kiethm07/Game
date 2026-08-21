#pragma once

#include <raylib.h>

/// A campfire the player can light, and the place a phase ends.
///
/// One thing, not two: the fire is the visual and the trigger is the same
/// point, so a level cannot end up with a campfire nobody can use or a
/// transition with nothing to look at. Placed from level data rather than
/// modelled into the map -- a campfire baked into VISUAL would be merged into a
/// terrain chunk at export and could never be lit, because raylib's glTF
/// loader discards mesh names and there would be nothing left to address.
struct Checkpoint {
  Vector3 position{0.0f, 0.0f, 0.0f};

  /// Degrees about Y. Only affects which way the logs lie.
  float yaw = 0.0f;

  /// Drawn as bare logs when false, and with the flame when true.
  bool lit = false;
};
