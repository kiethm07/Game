#pragma once

/// One place a run passes through.
struct PhaseEntry {
  /// Human name. Matches the `name` field inside the phase's level.json and the
  /// profile key in tools/make_castle_level.py. Logging and UI only — the Level
  /// takes its own name from the file, never from here.
  const char *name;

  /// Absolute path to the phase's level.json, built with ASSET_DIR exactly as
  /// AssetManifest.h's rows are.
  const char *levelPath;
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
    {"phase1_forest", ASSET_DIR "/levels/phase1_forest/level.json"},
    {"phase2_approach", ASSET_DIR "/levels/phase2_approach/level.json"},
    {"phase3_battlefield", ASSET_DIR "/levels/phase3_battlefield/level.json"},
};
