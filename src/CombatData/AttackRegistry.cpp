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
}
