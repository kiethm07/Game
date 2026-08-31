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

// The final boss is Mixamo's Mutant, and it is the FIRST asset here that was
// already rigged when it arrived -- so unlike the player and the miniboss there
// is no retarget pass at all. Its clips were downloaded on this character, and
// a fresh import of each was measured against the model's own rig before the
// pack was built: all eight carry the same 37 bones with the same names.
//
//   1. tools/build_finalboss_pack.py imports the eight FBXs from
//      `animation pack final boss` as one armature per clip, four of them
//      scratch. Reads finalboss.blend, writes pack_finalboss.blend, so the
//      hand-made source file is never touched.
//   1b. tools/make_finalboss_guard.py authors the crossed-arm block -- Guard,
//      GuardWalk, GuardImpact -- and the HitReact they are layered over.
//   1c. tools/make_finalboss_break.py authors PostureBreak (down on the left
//      knee, left fist planted) and trims Death out of `mutant dying`.
//   1d. tools/make_finalboss_moves.py generates the four strafes from Walk and
//      builds the three attacks, then deletes the scratch clips.
//   1e. tools/scale_finalboss.py takes the rig to 1.5x -- 2.792 m, a little
//      over the mini boss's 2.627 -- and writes pack_finalboss_scaled.blend.
//      LAST, because every pose the authoring passes solve is an absolute
//      world-metre target measured against the unscaled rig. Scaling the
//      armature object is all it does: merge_animations bakes that into the
//      mesh and rest bones and rescales the translation keys with it, so bone
//      lengths, skin and every clip's travel scale together. What does NOT
//      follow is anything in world metres on the C++ side -- body_height and
//      body_radius in Swordman's FinalBoss branch, the attack hitbox capsules
//      in AttackRegistry.cpp, and walk_speed/run_speed -- and those are set
//      to the same 1.5 by hand.
//   2. tools/merge_animations.py -> FinalBoss.glb
//   3. tools/bake_root_motion.py -> FinalBoss.rootmotion.glb
// tools/rebuild_finalboss.sh runs all of it, in that order, which is not
// negotiable: step 1 is a replacement and step 1d consumes what it deletes.
//
// 37 bones, not the 65 the other two carry. The Mutant has no left-hand finger
// bones at all -- its oversized left arm is ONE rigid club weighted entirely to
// `mixamorig:LeftForeArm`, running 0.953 m along a 0.268 m bone. Nothing in the
// runtime cares, because SwordmanAnimator resolves clips by name and the
// skinning shader does not count joints; but anything that poses this rig has
// to (see tools/finalboss_rig.py, LIMB_TIP).
//
// It carries 17 clips named for the states they serve, so descTable() hands it
// a table of its own -- the third, beside the ashigaru's and the miniboss's.
// There is no weapon mesh: this character's fists are the weapon, so the attack
// hitboxes in AttackRegistry.cpp are timed off the FISTS rather than off a
// blade, and none of the three attacks sets a trail.
//
// Attack_Jump is the only enemy clip in the game with authored travel that
// gameplay consumes: it leaps 2.56 m forward, and Swordman applies that through
// AttackData::usesRootMotion(). The other two are in place to 0.000 m, and so
// are all four strafes -- nothing reads a strafe's travel, and carrying any
// made every direction change fade a root cancellation against a fresh zero.

// The kimono swordsman. The only character here whose model was NOT already on
// a Mixamo rig and did not come from a Mixamo pack -- it arrived as a 105-bone
// 3ds Max Biped with a hidden duplicate of itself in the file. So it has a
// pipeline of its own, run end to end by tools/rebuild_kimono.sh:
//   1. tools/make_kimono_source.py reduces GH10_textured.blend to one clean
//      character: deletes the `Backup` collection (a complete second copy that
//      a hidden LayerCollection does NOT keep out of an export), joins the 183
//      loose head and hair pieces into one mesh and RIGID-binds them to `Head`
//      -- they arrived parented to nothing, which looks right in the bind pose
//      and detaches the moment anything animates -- and drops the second of the
//      two katanas. Writes kimono.blend.
//   2. tools/retarget_kimono.py moves it onto the Mixamo rig. Two things make
//      this one different from the other two retargets:
//        * THE SIDE NAMES ARE MIRRORED. Both rigs face -Y, but Mixamo's
//          LeftFoot is at x=+0.098 and this rig's L_Foot at x=-0.074, so `L_`
//          maps to `Right*`. Mapping by name would have built a mirrored
//          character -- every clip reversed, sword in the wrong fist -- which
//          reads as bad animation rather than as a bad table, so the script
//          asserts the measurement before it runs.
//        * 47 of its bones have no Mixamo counterpart and carry real weight
//          (the hakama, the haori panels, the blade). They are rebuilt onto
//          the Mixamo rig at their own rest positions rather than dropped.
//          Nothing animates them; they follow their parents through FK.
//      69 + 47 = 116 joints against MAX_BONE_NUM 128 in skinning.vs, and the
//      script refuses to exceed it.
//   3. tools/build_kimono_pack.py swaps the borrowed player clips for Mixamo's
//      Great Sword pack, every file chosen off a measurement of what it does
//      rather than off its name -- see SOURCES there.
//   4. tools/merge_animations.py -> KimonoEnemy.glb
//   5. tools/bake_root_motion.py -> KimonoEnemy.rootmotion.glb
//
// 16 clips, named for the states they serve, so descTable() hands it a table of
// its own -- the fourth. Two of the states in the brief this was built to have
// no clip and fall back the way the ashigaru's do: GuardWalk resolves to Guard
// and StrafeForward to Walk, because the greatsword pack ships neither a
// guarded walk nor a forward dash and neither is worth faking.
//
// The blade is `L_Katana`, one rigid bone off the hand -- the same arrangement
// as the miniboss's greatsword, so the runtime needs nothing for it. Its
// attacks in AttackRegistry.cpp are timed off the tip measured through the
// clips, not off the clip's length.

static const AssetEntry kAssets[] = {
    {AssetID::PLAYER_WOLF, "Sekiro.rootmotion.glb", "Sekiro.rootmotion.glb",
     RendererKind::SkinnedCharacter},
    {AssetID::ENEMY_ASHIGARU, "Paladin.rootmotion.glb", "Paladin.rootmotion.glb",
     RendererKind::SkinnedCharacter},
    {AssetID::ENEMY_MINIBOSS, "MiniBoss.rootmotion.glb", "MiniBoss.rootmotion.glb",
     RendererKind::SkinnedCharacter},
    {AssetID::ENEMY_FINALBOSS, "FinalBoss.rootmotion.glb", "FinalBoss.rootmotion.glb",
     RendererKind::SkinnedCharacter},
    {AssetID::ENEMY_KIMONO, "KimonoEnemy.rootmotion.glb", "KimonoEnemy.rootmotion.glb",
     RendererKind::SkinnedCharacter},
};
