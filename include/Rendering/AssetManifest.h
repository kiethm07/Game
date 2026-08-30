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

// The miniboss asset is built the same three-pass way as the player's (see
// above), from a separate copy of the source pack rather than pack_sekiro.blend:
//   1. tools/retarget_miniboss.py moves the Blood Knight model (imported from
//      ~/Documents/3D/Model/miniboss.blend, textured from knight-of-the-blood-
//      order/) off its own 64-bone rig onto the Mixamo rig, scaled to 1.5x the
//      stock Paladin placeholder's height before retargeting so bone lengths
//      feed the rebuild already at final size. Saves pack_miniboss.blend
//   1b. tools/add_miniboss_sword.py binds the greatsword into his right fist,
//      and tools/bake_sword_albedo.py paints its albedo. Both edit
//      pack_miniboss.blend in place, in that order -- the bake needs the prop
//      already at final scale with a UV island of its own.
//   1c. tools/build_miniboss_pack.py swaps the borrowed player clip set for
//      Mixamo's Great Sword pack, and tools/make_miniboss_guard.py authors the
//      three guard clips no pack ships. Both edit pack_miniboss.blend in place.
//      A clip added after that first build comes in through
//      tools/add_miniboss_clip.py, which does NOT re-run build_miniboss_pack --
//      that one is a replacement and would delete the guard set.
//   1d. tools/plant_clip_on_floor.py drops Death's hips until the body rests
//      on the floor its feet started on. A Mixamo clip's hip translation is
//      absolute metres authored for a rig a fifth shorter than this one, so
//      the fall stopped 0.09-0.15 m short and the corpse hovered. Only clips
//      that end lying or kneeling need this; re-run it if Death is replaced.
//   2. tools/merge_animations.py -> MiniBoss.glb
//   3. tools/bake_root_motion.py -> MiniBoss.rootmotion.glb
//
// It shares the player's Mixamo SKELETON but no longer its clip names: this
// asset carries 20 greatsword clips named after the states they serve, so
// SwordmanAnimator::descTable() switches on the AssetID and hands the miniboss
// its own table -- see AssetID::ENEMY_MINIBOSS passed into Swordman's
// constructor in EnemyFactory.cpp. Two clips are inherited from the player's
// pack because the greatsword set has no counterpart: Fall (its jump clips
// travel or land, none of them hover) and PostureBreak (the Paladin/player
// FallToKneel, which is what the deathblow window has always shown).
//
// The weapon is a SECOND MESH on the same skin, rigid-weighted to
// mixamorig:RightHand, so it needs nothing from the runtime: AssetManager's
// setupGpuSkinning attaches the skinning shader to every material, and the prop
// carries its own. It is 2.48 m long, sized so the hilt fills this character's
// fist exactly as it filled the fist it was authored for.
//
// That length is not free. tools/verify_miniboss_sword.py measures how far the
// blade drops below the character's feet in each clip. Of the four this enemy
// actually attacks with, Attack_H never reaches the floor at all and the two
// combos only break it after their last hit has landed: Combo_3 for 0.18 s at
// 0.32 m and Combo_2 for 0.63 s at 0.51 m, which is its overhead chop burying
// the blade and then dragging it back out. Measure any further swing before
// wiring it up -- Attack_3 reaches 4.07 m, and there is no runtime clamp.

static const AssetEntry kAssets[] = {
    {AssetID::PLAYER_WOLF, "Sekiro.rootmotion.glb", "Sekiro.rootmotion.glb",
     RendererKind::SkinnedCharacter},
    {AssetID::ENEMY_ASHIGARU, "Paladin.rootmotion.glb", "Paladin.rootmotion.glb",
     RendererKind::SkinnedCharacter},
    {AssetID::ENEMY_MINIBOSS, "MiniBoss.rootmotion.glb", "MiniBoss.rootmotion.glb",
     RendererKind::SkinnedCharacter},
};
