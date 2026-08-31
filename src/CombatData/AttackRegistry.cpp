#include <CombatData/AttackRegistry.h>

void AttackRegistry::InitializeCatalog() {
  // Phase durations are authored against the clip they play, so the state
  // machine ends when the animation does. The sums below sit just under each
  // clip's playable length (the authored length minus one frame — see
  // RootMotion::Track::duration), which leaves the follow-through intact
  // without holding on the final pose. Re-tune these whenever the clip
  // changes; the clip is the reference, not the other way round.
  //
  // Root motion is on only where the clip actually travels. Slash is fully
  // in-place, so it stays pinned; Slash_3 steps in and back out (peak 0.49
  // units, net ~0); Attack_2 is a 2.7-unit lunge, which reads as the
  // finisher's commitment.

  // Slash: 1.53s authored, in place.
  AttackData light1(0.55f, 0.20f, 0.76f, "Slash", false);
  // Sweep capsule from right to left (simulating a horizontal slash)
  light1.addHitBoxDef(HitBoxDefinition::createCapsule({1.0f, 0.9f, 1.0f}, {-1.0f, 0.9f, 1.0f}, 0.5f, 25.0f, 15.0f));
  light1.setTrail(true, 0.22f, {0.0f, 1.25f, 0.0f}, {0.0f, 0.1f, 0.0f});
  attack_catalog.emplace(AttackID::PlayerLight1, light1);

  // Slash_3: 1.60s authored, small step in and out.
  AttackData light2(0.55f, 0.20f, 0.83f, "Slash_3", true);
  // Sweep capsule from left to right (simulating a horizontal slash)
  light2.addHitBoxDef(HitBoxDefinition::createCapsule({-1.0f, 0.9f, 1.0f}, {1.0f, 0.9f, 1.0f}, 0.5f, 25.0f, 15.0f));
  light2.setTrail(true, 0.22f, {0.0f, 1.25f, 0.0f}, {0.0f, 0.1f, 0.0f});
  attack_catalog.emplace(AttackID::PlayerLight2, light2);

  // Attack_2: 1.33s authored, 2.7-unit forward lunge.
  AttackData heavy1(0.45f, 0.18f, 0.68f, "Attack_2", true);
  // Thrust capsule straight forward
  heavy1.addHitBoxDef(HitBoxDefinition::createCapsule({0.0f, 0.9f, 0.0f}, {0.0f, 0.9f, 2.0f}, 0.6f, 35.0f, 25.0f));
  heavy1.setTrail(true, 0.25f, {0.0f, 1.25f, 0.0f}, {0.0f, 0.1f, 0.0f});
  attack_catalog.emplace(AttackID::PlayerHeavyFinisher, heavy1);

  // The deathblow
  AttackData exec(0.55f, 0.20f, 0.76f, "Slash", false);
  // Guarantee hit during takedown regardless of physics separation by using a large capsule extending forward
  exec.addHitBoxDef(HitBoxDefinition::createCapsule({0.0f, 0.9f, 0.0f}, {0.0f, 0.9f, 2.0f}, 1.5f, 9999.0f, 15.0f));
  exec.setTrail(true, 0.30f, {0.0f, 1.25f, 0.0f}, {0.0f, 0.1f, 0.0f});
  attack_catalog.emplace(AttackID::PlayerExecution, exec);

  // ---------------------------------------------------------------------
  // The mini boss, on the greatsword pack (assets/MiniBoss.rootmotion.glb).
  //
  // Every window below was MEASURED, not eyeballed: the sword mesh is rigid
  // bound to `mixamorig:RightHand`, so its tip can be carried through each
  // clip's joint hierarchy and the frames where the blade actually crosses the
  // space in front of the character read straight off that trajectory. The
  // times quoted per swing are those frames, and the capsule for each is sized
  // to the arc the blade swept during them. What this buys is the thing the
  // timings are for -- a hitbox that goes live on the frame the blade is on
  // screen where it looks like it should hurt, twice in the two-hit clip and
  // three times in the three-hit one.
  //
  // Reach is longer than the player's 2.0 m: these capsules catch a player out
  // to ~2.4 m on the sweeps and ~3.0 m on the chop, which is the difference
  // between a katana and a 2.1 m two-hander and is meant to be felt.
  //
  // Root motion is off on all three. The clips are in-place (bake_root_motion
  // reports net horizontal 0.000 for each), and SwordmanAnimator drops clip
  // travel for enemies regardless.
  //
  // The trail vectors are the sword's own geometry in hand-bone space -- tip
  // (1.73, 1.17, -0.24), grip (0.14, 0.15, 0.03). The player's {0, 1.25, 0}
  // is the katana pointing up the hand's +Y and is simply the wrong axis for
  // this weapon, which lies along the hand's +X.
  const Vector3 kMiniBossBlade = {1.73f, 1.17f, -0.24f};
  const Vector3 kMiniBossHilt = {0.14f, 0.15f, 0.03f};

  // Attack_H ("standing melee attack horizontal"): 147 keyframes, 2.417s
  // playable, in place. One cut, blade crossing the front t=0.90-1.10 (the tip
  // peaks 3.18 forward at t=0.98). 0.90 + 0.20 + 1.31 = 2.41. The long tail is
  // recovery, and a punishable one, because that is what the clip does after
  // the cut.
  AttackData mb_swing(0.90f, 0.20f, 1.31f, "Attack_H", false);
  mb_swing.addHitBoxDef(HitBoxDefinition::createCapsule(
      {2.0f, 1.30f, 1.30f}, {-2.0f, 1.30f, 1.30f}, 0.70f, 35.0f, 20.0f));
  mb_swing.setTrail(true, 0.28f, kMiniBossBlade, kMiniBossHilt);
  attack_catalog.emplace(AttackID::MiniBossSwing, mb_swing);

  // Combo_3 ("standing melee combo attack ver. 3"): 167 keyframes, 2.750s
  // playable, in place. Two cuts:
  //   1. t=0.98-1.17, right to left, tip peaks 2.99 forward at chest height
  //   2. t=1.60-1.84, left to right and dropping, tip peaks 2.15 forward
  // 0.95 + 0.22 + 0.43 + 0.24 + 0.89 = 2.73.
  AttackData mb_double(0.95f, 0.22f, 0.89f, "Combo_3", false);
  mb_double.addHitBoxDef(HitBoxDefinition::createCapsule(
      {1.9f, 1.25f, 1.25f}, {-1.9f, 1.25f, 1.25f}, 0.70f, 28.0f, 16.0f));
  mb_double.addSwing(0.43f, 0.24f);
  mb_double.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-1.9f, 1.15f, 1.15f}, {1.9f, 1.15f, 1.15f}, 0.70f, 32.0f, 18.0f));
  mb_double.setTrail(true, 0.28f, kMiniBossBlade, kMiniBossHilt);
  attack_catalog.emplace(AttackID::MiniBossDoubleSwing, mb_double);

  // Combo_2 ("standing melee combo attack ver. 2"): 254 keyframes, 4.200s
  // playable, in place. Three hits:
  //   1. t=0.98-1.17, right to left, tip peaks 2.85 forward
  //   2. t=1.60-1.80, left to right, tip peaks 2.70 forward
  //   3. t=2.60-2.80, overhead chop -- the tip falls from 3.89 up to the floor
  //      and out to 3.7 forward, so this one is a NARROW capsule standing in
  //      the vertical plane the blade comes down through, not a lateral sweep.
  // 0.95 + 0.22 + 0.43 + 0.20 + 0.80 + 0.20 + 1.38 = 4.18. That 1.38s tail is
  // the sword buried in the ground and dragged back out; it is the widest
  // opening the boss gives and it is left at very nearly its authored length.
  AttackData mb_triple(0.95f, 0.22f, 1.38f, "Combo_2", false);
  mb_triple.addHitBoxDef(HitBoxDefinition::createCapsule(
      {1.9f, 1.20f, 1.25f}, {-1.9f, 1.20f, 1.25f}, 0.70f, 25.0f, 15.0f));
  mb_triple.addSwing(0.43f, 0.20f);
  mb_triple.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-1.9f, 1.30f, 1.25f}, {1.9f, 1.30f, 1.25f}, 0.70f, 25.0f, 15.0f));
  mb_triple.addSwing(0.80f, 0.20f);
  mb_triple.addHitBoxDef(HitBoxDefinition::createCapsule(
      {0.0f, 1.60f, 0.90f}, {0.0f, 0.35f, 1.90f}, 0.70f, 45.0f, 25.0f));
  mb_triple.setTrail(true, 0.28f, kMiniBossBlade, kMiniBossHilt);
  attack_catalog.emplace(AttackID::MiniBossTripleSwing, mb_triple);

  // ---------------------------------------------------------------------
  // The final boss, on the Mutant pack (assets/FinalBoss.rootmotion.glb).
  //
  // Timed off the FISTS, not a blade: this character carries no weapon, so the
  // trajectory that matters is its own hands. Both are carried through each
  // clip's joint hierarchy the way the miniboss's sword tip is -- and on this
  // rig that is not the hand BONE. The Mutant has no `mixamorig:LeftHand`
  // vertex group at all; its oversized left arm is one rigid club weighted to
  // the forearm and reaching 0.953 m past the elbow, so the visible left fist
  // is measured out along the FOREARM (tools/finalboss_rig.py, LIMB_TIP). The
  // right arm is ordinary.
  //
  // A window is a frame range where a fist is both forward of the body and
  // moving faster than 6 m/s. The times quoted per swing are those frames.
  //
  // REACHES BELOW ARE QUOTED AT AUTHORING SCALE, and the capsules are those
  // figures times 1.5 -- tools/scale_finalboss.py takes the whole rig to
  // 2.792 m as the last step before export, so every measurement taken in the
  // .blend is 1/1.5 of what the shipped asset does. Times are unaffected:
  // scaling a rig does not change when a fist crosses the body.
  // Every clip's playable length is one frame shorter than its authored length
  // (RootMotion::Track::duration), and the phase sums below are against the
  // playable figure.
  //
  // No trails. setTrail draws a weapon arc between a hilt and a tip, which is
  // a thing this character does not have.

  // `Attack` (a right jab, then a left club swing): 60 frames, 1.967 s
  // playable, in place to 0.000 m. Two hits:
  //   1. t=0.17-0.30, the jab, fist reaching 0.91 m forward at 0.59 high
  //   2. t=1.47-1.60, the club coming across low, 1.31 m out at 0.48
  // 0.17 + 0.13 + 1.17 + 0.13 + 0.37 = 1.97. The long gap between them is the
  // punch's own recovery, which the swing is appended after rather than into.
  AttackData fb_punch(0.17f, 0.13f, 0.37f, "Attack", false);
  fb_punch.addHitBoxDef(HitBoxDefinition::createCapsule(
      {0.000f, 1.125f, 0.450f}, {0.000f, 0.900f, 1.950f}, 0.825f, 30.0f, 18.0f));
  fb_punch.addSwing(1.17f, 0.13f);
  fb_punch.addHitBoxDef(HitBoxDefinition::createCapsule(
      {2.250f, 0.825f, 1.425f}, {-2.250f, 0.825f, 1.425f}, 0.900f, 35.0f, 20.0f));
  attack_catalog.emplace(AttackID::FinalBossPunch, fb_punch);

  // `Attack_Rapid` (five alternating swipes): 97 frames, 3.200 s playable, in
  // place to 0.000 m. Five hits, alternating arms -- the odd ones are the left
  // club sweeping low and wide, the even ones the shorter right arm coming over
  // the top, which is why their reaches and heights differ:
  //   1. t=0.83-1.00, left,  1.52 m out at 1.13
  //   2. t=1.13-1.40, right, 0.95 m out at 1.89
  //   3. t=1.77-1.93, left,  1.52 m out at 1.13
  //   4. t=2.07-2.33, right, 0.95 m out at 1.89
  //   5. t=2.70-2.83, left,  1.31 m out at 0.49
  // 0.83 + 0.17 + 0.13 + 0.27 + 0.37 + 0.17 + 0.14 + 0.26 + 0.37 + 0.13 + 0.36
  // = 3.20. Per-hit damage is deliberately low: five of these landing in full
  // is 110, which is more than any single miniboss combo and is meant to be the
  // punish for standing in it.
  AttackData fb_flurry(0.83f, 0.17f, 0.36f, "Attack_Rapid", false);
  fb_flurry.addHitBoxDef(HitBoxDefinition::createCapsule(
      {2.400f, 1.650f, 1.650f}, {-2.400f, 1.650f, 1.650f}, 0.900f, 22.0f, 14.0f));
  fb_flurry.addSwing(0.13f, 0.27f);
  fb_flurry.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-1.800f, 2.625f, 1.275f}, {1.800f, 2.025f, 1.275f}, 0.825f, 22.0f, 14.0f));
  fb_flurry.addSwing(0.37f, 0.17f);
  fb_flurry.addHitBoxDef(HitBoxDefinition::createCapsule(
      {2.400f, 1.650f, 1.650f}, {-2.400f, 1.650f, 1.650f}, 0.900f, 22.0f, 14.0f));
  fb_flurry.addSwing(0.14f, 0.26f);
  fb_flurry.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-1.800f, 2.625f, 1.275f}, {1.800f, 2.025f, 1.275f}, 0.825f, 22.0f, 14.0f));
  fb_flurry.addSwing(0.37f, 0.13f);
  fb_flurry.addHitBoxDef(HitBoxDefinition::createCapsule(
      {2.250f, 0.825f, 1.425f}, {-2.250f, 0.825f, 1.425f}, 0.900f, 22.0f, 14.0f));
  attack_catalog.emplace(AttackID::FinalBossFlurry, fb_flurry);

  // `Attack_Jump` (the leap, a landing slam, then two swings): 102 frames,
  // 3.367 s playable, and the ONE enemy attack in the game that travels --
  // 2.556 m forward (1.704 at authoring scale), consumed through
  // usesRootMotion() below. Three hits:
  //   1. t=1.33-1.67, the landing slam, both fists coming down
  //   2. t=2.27-2.57, left club across
  //   3. t=2.70-3.00, right arm over the top
  // 1.33 + 0.34 + 0.60 + 0.30 + 0.13 + 0.30 + 0.37 = 3.37.
  //
  // The airborne window at t=0.33-0.67 is NOT a hit: it is the arms coming up
  // as the character leaves the ground, and a hitbox there would catch a player
  // standing where the boss started rather than where it lands.
  //
  // Capsule reach is quoted from the body, not from the clip. The measured fist
  // positions run to 3.2 m forward, but 1.70 of that is the leap itself, and
  // the capsule is placed in the character's own space AFTER root motion has
  // moved it -- so counting the travel twice would give the slam nearly double
  // the reach it looks like it has.
  AttackData fb_leap(1.33f, 0.34f, 0.37f, "Attack_Jump", true);
  fb_leap.addHitBoxDef(HitBoxDefinition::createCapsule(
      {1.350f, 2.100f, 0.600f}, {-1.350f, 0.300f, 2.250f}, 1.125f, 55.0f, 32.0f));
  fb_leap.addSwing(0.60f, 0.30f);
  fb_leap.addHitBoxDef(HitBoxDefinition::createCapsule(
      {2.550f, 1.350f, 1.650f}, {-2.550f, 1.350f, 1.650f}, 0.975f, 32.0f, 20.0f));
  fb_leap.addSwing(0.13f, 0.30f);
  fb_leap.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-1.950f, 2.550f, 1.350f}, {1.950f, 1.950f, 1.350f}, 0.900f, 32.0f, 20.0f));
  attack_catalog.emplace(AttackID::FinalBossLeap, fb_leap);
}
