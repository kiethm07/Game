#pragma once
#include <Rendering/AssetID.h>

/// Which IEntityRenderer strategy an asset is drawn with.
enum class RendererKind {
  SkinnedCharacter, ///< SkinnedEntityRenderer (GPU-skinned GLB)
  DebugCube,        ///< DebugCubeRenderer (placeholder proxy)
};

struct AssetEntry {
  AssetID id;

  // Both are RELATIVE to the asset root, not absolute. They used to be built
  // with ASSET_DIR, which baked the build machine's source directory into the
  // binary; they are now resolved through assets::path() where they are loaded,
  // so a packaged copy finds them beside itself. See Core/AssetPaths.h.
  const char *modelPath; ///< nullptr → no model to load
  const char *animPath;  ///< nullptr → no animations to load
  RendererKind renderer = RendererKind::SkinnedCharacter;
  const AssetID *sharedModelId = nullptr; ///< if set, alias model to pointed-to source
  const AssetID *sharedAnimId = nullptr; ///< if set, alias animations to pointed-to source
};

// The player asset is built in three passes from ~/Documents/3D/pack.blend:
//   1. tools/retarget_sekiro.py moves the LowPolySekiroRigged model off its own
//      40-bone rig onto the 69-bone Mixamo rig the clips animate, by rebuilding
//      the Mixamo REST skeleton onto the model's joints -- legal only because
//      every clip is pure rotation plus Hips translation. Saves pack_sekiro.blend
//   2. tools/merge_animations.py folds the 60 per-clip Mixamo armatures into
//      one skinned GLB at scale 1.0 -> Sekiro.glb
//   3. tools/bake_root_motion.py moves each clip's horizontal travel from
//      mixamorig:Hips onto a dedicated `Root` bone -> Sekiro.rootmotion.glb
// Load only the third. Pointing this at Sekiro.glb reverts bone 0 to the hips
// and root motion starts picking up hip sway.

// The ashigaru has no model of its own yet, so it borrows the player's rather
// than the single-clip Walk.glb it used to draw with: one skeleton, one set of
// 60 named clips, which is what lets SwordmanAnimator's table name "Slash" and
// "Impact_2" the way the player's does. Aliased, not loaded a second time —
// both IDs resolve to the one Model and the one animation array, and the
// skinning shader re-uploads the bone matrices per draw, so each entity poses
// it independently.
//
// Swapping in a real ashigaru asset is this pointer becoming a path, plus
// whatever clip names the new asset carries.
static const AssetID kAshigaruSource = AssetID::PLAYER_WOLF;

static const AssetEntry kAssets[] = {
    {AssetID::PLAYER_WOLF, "Sekiro.rootmotion.glb", "Sekiro.rootmotion.glb",
     RendererKind::SkinnedCharacter},
    {AssetID::ENEMY_ASHIGARU, nullptr, nullptr, RendererKind::SkinnedCharacter,
     &kAshigaruSource, &kAshigaruSource},
};
