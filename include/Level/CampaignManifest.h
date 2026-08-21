#pragma once

/// One place a run passes through.
struct PhaseEntry {
  /// Human name. Matches the `name` field inside the phase's level.json and the
  /// profile key in tools/make_castle_level.py. Logging and UI only — the Level
  /// takes its own name from the file, never from here.
  const char *name;

  /// Path to the phase's level.json, RELATIVE to the asset root, exactly as
  /// AssetManifest.h's rows are. Campaign::currentLevelPath() resolves it
  /// through assets::path() so a packaged build finds it beside the
  /// executable rather than in the source tree it was compiled against.
  const char *levelPath;

  /// Where this phase ends: the campfire the player lights to move on.
  ///
  /// Here rather than in the level's own data because it is a *campaign* fact,
  /// not a map fact -- a level does not know it is followed by anything, and
  /// this table is already the only place that does. It also means the exit and
  /// the phase order cannot disagree: they are one row.
  ///
  /// X and Z only. The height is resolved at load by dropping onto the
  /// collision mesh (SpawnGround::highestUnder), the same way a `y`-less enemy
  /// spawn is, so these two numbers stay right if the terrain is re-exported.
  ///
  /// `hasExit` is false for the last phase, which ends the run rather than
  /// leading anywhere -- a campfire there would promise something that is not
  /// on the other side of it.
  bool hasExit;
  float exitX;
  float exitZ;
};

/// The campaign, in order.
///
/// This table IS the order. There is no `next` field and no graph: Campaign
/// walks it by index, so adding a phase is one row and nothing else, and the
/// order on screen is the order on the page.
///
/// Only the three phaseN_* directories are here. assets/levels also carries
/// castle_approach, castle_full, forest, greybox, stress, phase3_interior and
/// phase4_battlefield, all of which load — they are greyboxes, build
/// intermediates and test maps, not places a run passes through. Playing one
/// means pointing a row here at it, which is the whole mechanism.
static const PhaseEntry kCampaignPhases[] = {
    {"phase1_forest", "levels/phase1_forest/level.json",
     true, -53.79f, -21.63f},
    {"phase2_approach", "levels/phase2_approach/level.json",
     true, -105.13f, -72.27f},
    // The run ends here, so there is nothing to walk to.
    {"phase3_battlefield", "levels/phase3_battlefield/level.json",
     false, 0.0f, 0.0f},
};
