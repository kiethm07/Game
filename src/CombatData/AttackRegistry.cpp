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
  light1.addHitBoxDef(HitBoxDefinition::createCapsule({1.0f, 0.9f, 1.0f}, {-1.0f, 0.9f, 1.0f}, 0.5f, 38.0f, 23.0f));
  light1.setTrail(true, 0.22f, {0.0f, 1.25f, 0.0f}, {0.0f, 0.1f, 0.0f});
  attack_catalog.emplace(AttackID::PlayerLight1, light1);

  // Slash_3: 1.60s authored, small step in and out.
  AttackData light2(0.55f, 0.20f, 0.83f, "Slash_3", true);
  // Sweep capsule from left to right (simulating a horizontal slash)
  light2.addHitBoxDef(HitBoxDefinition::createCapsule({-1.0f, 0.9f, 1.0f}, {1.0f, 0.9f, 1.0f}, 0.5f, 38.0f, 23.0f));
  light2.setTrail(true, 0.22f, {0.0f, 1.25f, 0.0f}, {0.0f, 0.1f, 0.0f});
  attack_catalog.emplace(AttackID::PlayerLight2, light2);

  // Attack_2: 1.33s authored, 2.7-unit forward lunge.
  AttackData heavy1(0.45f, 0.18f, 0.68f, "Attack_2", true);
  // Thrust capsule straight forward
  heavy1.addHitBoxDef(HitBoxDefinition::createCapsule({0.0f, 0.9f, 0.0f}, {0.0f, 0.9f, 2.0f}, 0.6f, 53.0f, 38.0f));
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

  // The katana in hand-bone space. 0.83 m of blade -- half the miniboss's
  // greatsword -- lying along the hand's +Y with the tip a little forward,
  // which is the ordinary katana grip rather than the greatsword's +X.
  const Vector3 kKimonoBlade = {0.05f, 0.83f, 0.10f};
  const Vector3 kKimonoHilt = {0.01f, 0.06f, 0.01f};

  // Attack_Spin: 199 keyframes, 3.300s playable, in place. The boss's special,
  // and the one attack here that is AUTHORED rather than downloaded --
  // tools/make_miniboss_spin.py builds it, and its report is where every number
  // below comes from. It cocks the greatsword back one-handed over 0.40s, HOLDS
  // that pose for 0.50s, and then turns three times ANTICLOCKWISE seen from
  // above, carrying the blade level at 1.35 m, tip 2.6 m out from the body's
  // own axis. The direction is the clip's: make_miniboss_spin mirrors the
  // source's footwork rather than spinning its hips the other way.
  //
  // Three turns, three hit windows: one per revolution, timed to the pass the
  // blade makes across the FRONT of the boss. Each window is the time the
  // measured tip spends inside 50 degrees either side of straight ahead -- read
  // off the clip, not assumed, because the turn runs between 9 and 28 deg a
  // frame and where the pass falls is the clip's business rather than a guess.
  //
  // That 50 is the capsule's own half-angle: the bar below reaches 1.80 m
  // either side at 1.50 m forward, so its ends sit at atan2(1.80, 1.50) = 50.2
  // deg. The window is open exactly while the blade is inside the arc the
  // hitbox covers, which is what keeps the two from disagreeing when either is
  // retuned.
  //
  // What this is NOT is a hitbox that follows the blade all the way round. The
  // sweep behind and beside the boss is picture; only the pass in front can
  // hit. So the attack is answered by not being in front of it when the blade
  // comes round -- three separate times, with two thirds of a second to read
  // each one.
  //
  // In FRAMES at 60 Hz, not seconds. A window can only start and end on a
  // frame, so a duration that falls between two of them rounds up and the
  // windows drift late. The clip is a 60 Hz clip (raylib resamples every
  // animation to it), so quantising is exact:
  // 84 + 10 + 30 + 9 + 27 + 8 + 30 = 198 frames, which is the clip's own 3.300s.
  //
  // The three passes are not identical. The turn accelerates through the clip
  // and its last revolution carries a follow-through, so each crosses a little
  // quicker than the one before -- 10 frames, then 9, then 8. Measured, not
  // averaged into a single number that would be wrong twice.
  const int kSpinWindup = 84;             // to the first pass: cock-back, hold
  const int kSpinPass[3][2] = {{0, 10},   // {gap since the last pass, live}
                               {30, 9},
                               {27, 8}};
  const int kSpinRecover = 30;            // out of the last turn
  // Half a frame of slack against the countdown landing exactly on zero, which
  // would cost a window a frame either way depending on the float.
  auto spin_frames = [](int frames) { return (frames - 0.5f) / 60.0f; };

  // MiniBoss Spin (3 turns)
  AttackData mb_spin1(1.39f, 0.16f, 0.00f, "Attack_Spin", 0.00f, false);
  mb_spin1.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-1.80f, 1.35f, 1.50f}, {1.80f, 1.35f, 1.50f}, 0.80f, 60.0f, 36.0f));
  mb_spin1.setTrail(true, 0.30f, kMiniBossBlade, kMiniBossHilt);
  mb_spin1.setAdvance(2.6f, 1.2f, 90.0f);
  attack_catalog.emplace(AttackID::MiniBossSpin1, mb_spin1);

  AttackData mb_spin2(0.49f, 0.14f, 0.00f, "Attack_Spin", 1.55f, false);
  mb_spin2.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-1.80f, 1.35f, 1.50f}, {1.80f, 1.35f, 1.50f}, 0.80f, 60.0f, 36.0f));
  mb_spin2.setTrail(true, 0.30f, kMiniBossBlade, kMiniBossHilt);
  mb_spin2.setAdvance(2.6f, 1.2f, 90.0f);
  attack_catalog.emplace(AttackID::MiniBossSpin2, mb_spin2);

  AttackData mb_spin3(0.44f, 0.12f, 0.49f, "Attack_Spin", 2.18f, false);
  mb_spin3.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-1.80f, 1.35f, 1.50f}, {1.80f, 1.35f, 1.50f}, 0.80f, 60.0f, 36.0f));
  mb_spin3.setTrail(true, 0.30f, kMiniBossBlade, kMiniBossHilt);
  mb_spin3.setAdvance(2.6f, 1.2f, 90.0f);
  attack_catalog.emplace(AttackID::MiniBossSpin3, mb_spin3);

  // MiniBoss Double Swing (Combo_3)
  AttackData mb_double1(0.95f, 0.22f, 0.00f, "Combo_3", 0.00f, false);
  mb_double1.addHitBoxDef(HitBoxDefinition::createCapsule(
      {1.9f, 1.25f, 1.25f}, {-1.9f, 1.25f, 1.25f}, 0.70f, 56.0f, 32.0f));
  mb_double1.setTrail(true, 0.28f, kMiniBossBlade, kMiniBossHilt);
  attack_catalog.emplace(AttackID::MiniBossDoubleSwing1, mb_double1);

  AttackData mb_double2(0.43f, 0.24f, 0.89f, "Combo_3", 1.17f, false);
  mb_double2.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-1.9f, 1.15f, 1.15f}, {1.9f, 1.15f, 1.15f}, 0.70f, 64.0f, 36.0f));
  mb_double2.setTrail(true, 0.28f, kMiniBossBlade, kMiniBossHilt);
  attack_catalog.emplace(AttackID::MiniBossDoubleSwing2, mb_double2);

  // MiniBoss Triple Swing (Combo_2)
  AttackData mb_triple1(0.95f, 0.22f, 0.00f, "Combo_2", 0.00f, false);
  mb_triple1.addHitBoxDef(HitBoxDefinition::createCapsule(
      {1.9f, 1.20f, 1.25f}, {-1.9f, 1.20f, 1.25f}, 0.70f, 50.0f, 30.0f));
  mb_triple1.setTrail(true, 0.28f, kMiniBossBlade, kMiniBossHilt);
  attack_catalog.emplace(AttackID::MiniBossTripleSwing1, mb_triple1);

  AttackData mb_triple2(0.43f, 0.20f, 0.00f, "Combo_2", 1.17f, false);
  mb_triple2.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-1.9f, 1.30f, 1.25f}, {1.9f, 1.30f, 1.25f}, 0.70f, 50.0f, 30.0f));
  mb_triple2.setTrail(true, 0.28f, kMiniBossBlade, kMiniBossHilt);
  attack_catalog.emplace(AttackID::MiniBossTripleSwing2, mb_triple2);

  AttackData mb_triple3(0.80f, 0.20f, 1.38f, "Combo_2", 1.80f, false);
  mb_triple3.addHitBoxDef(HitBoxDefinition::createCapsule(
      {0.0f, 1.60f, 0.90f}, {0.0f, 0.35f, 1.90f}, 0.70f, 90.0f, 50.0f));
  mb_triple3.setTrail(true, 0.28f, kMiniBossBlade, kMiniBossHilt);
  attack_catalog.emplace(AttackID::MiniBossTripleSwing3, mb_triple3);

  // ---------------------------------------------------------------------
  // The final boss, on the Mutant pack (assets/FinalBoss.rootmotion.glb).
  // ---------------------------------------------------------------------

  // Combo 1: Punch (Jab + Club)
  AttackData fb_jab(0.17f, 0.13f, 0.37f, "Attack", 0.00f, false);
  fb_jab.addHitBoxDef(HitBoxDefinition::createCapsule(
      {0.000f, 1.125f, 0.450f}, {0.000f, 0.900f, 1.950f}, 0.825f, 60.0f, 36.0f));
  attack_catalog.emplace(AttackID::FinalBossJab, fb_jab);

  AttackData fb_club(0.80f, 0.13f, 0.37f, "Attack", 0.67f, false);
  fb_club.addHitBoxDef(HitBoxDefinition::createCapsule(
      {2.250f, 0.825f, 1.425f}, {-2.250f, 0.825f, 1.425f}, 0.900f, 70.0f, 40.0f));
  attack_catalog.emplace(AttackID::FinalBossClub, fb_club);

  // Combo 2: Flurry (5 alternating swipes)
  AttackData fb_flurry1(0.83f, 0.17f, 0.00f, "Attack_Rapid", 0.00f, false);
  fb_flurry1.addHitBoxDef(HitBoxDefinition::createCapsule(
      {2.400f, 1.650f, 1.650f}, {-2.400f, 1.650f, 1.650f}, 0.900f, 44.0f, 28.0f));
  attack_catalog.emplace(AttackID::FinalBossFlurry1, fb_flurry1);

  AttackData fb_flurry2(0.13f, 0.27f, 0.00f, "Attack_Rapid", 1.00f, false);
  fb_flurry2.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-1.800f, 2.625f, 1.275f}, {1.800f, 2.025f, 1.275f}, 0.825f, 44.0f, 28.0f));
  attack_catalog.emplace(AttackID::FinalBossFlurry2, fb_flurry2);

  AttackData fb_flurry3(0.37f, 0.17f, 0.00f, "Attack_Rapid", 1.40f, false);
  fb_flurry3.addHitBoxDef(HitBoxDefinition::createCapsule(
      {2.400f, 1.650f, 1.650f}, {-2.400f, 1.650f, 1.650f}, 0.900f, 44.0f, 28.0f));
  attack_catalog.emplace(AttackID::FinalBossFlurry3, fb_flurry3);

  AttackData fb_flurry4(0.14f, 0.26f, 0.00f, "Attack_Rapid", 1.93f, false);
  fb_flurry4.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-1.800f, 2.625f, 1.275f}, {1.800f, 2.025f, 1.275f}, 0.825f, 44.0f, 28.0f));
  attack_catalog.emplace(AttackID::FinalBossFlurry4, fb_flurry4);

  AttackData fb_flurry5(0.37f, 0.13f, 0.36f, "Attack_Rapid", 2.33f, false);
  fb_flurry5.addHitBoxDef(HitBoxDefinition::createCapsule(
      {2.250f, 0.825f, 1.425f}, {-2.250f, 0.825f, 1.425f}, 0.900f, 44.0f, 28.0f));
  attack_catalog.emplace(AttackID::FinalBossFlurry5, fb_flurry5);

  // Combo 3: Leap (Leap slam + 2 swings)
  AttackData fb_leap1(1.33f, 0.34f, 0.00f, "Attack_Jump", 0.00f, true);
  fb_leap1.addHitBoxDef(HitBoxDefinition::createCapsule(
      {1.350f, 2.100f, 0.600f}, {-1.350f, 0.300f, 2.250f}, 1.125f, 110.0f, 64.0f));
  attack_catalog.emplace(AttackID::FinalBossLeapSlam, fb_leap1);

  AttackData fb_leap2(0.60f, 0.30f, 0.00f, "Attack_Jump", 1.67f, false);
  fb_leap2.addHitBoxDef(HitBoxDefinition::createCapsule(
      {2.550f, 1.350f, 1.650f}, {-2.550f, 1.350f, 1.650f}, 0.975f, 64.0f, 40.0f));
  attack_catalog.emplace(AttackID::FinalBossLeapSweep, fb_leap2);

  AttackData fb_leap3(0.13f, 0.30f, 0.37f, "Attack_Jump", 2.57f, false);
  fb_leap3.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-1.950f, 2.550f, 1.350f}, {1.950f, 1.950f, 1.350f}, 0.900f, 64.0f, 40.0f));
  attack_catalog.emplace(AttackID::FinalBossLeapOverhead, fb_leap3);

  // ---------------------------------------------------------------------
  // The kimono swordsman.
  // ---------------------------------------------------------------------

  // Combo 1: Single Slash
  AttackData km_swing(0.60f, 0.20f, 0.47f, "Attack", 0.00f, false);
  km_swing.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-0.35f, 1.45f, 1.10f}, {0.80f, 0.45f, 1.10f}, 0.55f, 60.0f, 36.0f));
  attack_catalog.emplace(AttackID::KimonoSwing, km_swing);

  // Combo 2: Heavy Cleave (3 cuts)
  AttackData km_cleave1(0.98f, 0.12f, 0.00f, "Combo_1", 0.00f, false);
  km_cleave1.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-1.20f, 0.90f, 1.05f}, {1.20f, 0.90f, 1.05f}, 0.55f, 52.0f, 28.0f));
  km_cleave1.setTrail(true, 0.26f, kKimonoBlade, kKimonoHilt);
  attack_catalog.emplace(AttackID::KimonoCleave1, km_cleave1);

  AttackData km_cleave2(0.50f, 0.12f, 0.00f, "Combo_1", 1.10f, false);
  km_cleave2.addHitBoxDef(HitBoxDefinition::createCapsule(
      {1.20f, 0.78f, 1.05f}, {-1.20f, 0.78f, 1.05f}, 0.55f, 52.0f, 28.0f));
  km_cleave2.setTrail(true, 0.26f, kKimonoBlade, kKimonoHilt);
  attack_catalog.emplace(AttackID::KimonoCleave2, km_cleave2);

  AttackData km_cleave3(0.84f, 0.18f, 1.46f, "Combo_1", 1.72f, false);
  km_cleave3.addHitBoxDef(HitBoxDefinition::createCapsule(
      {0.0f, 1.70f, 1.15f}, {0.0f, 0.15f, 1.60f}, 0.62f, 76.0f, 44.0f));
  km_cleave3.setTrail(true, 0.26f, kKimonoBlade, kKimonoHilt);
  attack_catalog.emplace(AttackID::KimonoCleave3, km_cleave3);

  // Combo 3: Lunge (3 cuts with root motion)
  AttackData km_lunge1(0.45f, 0.26f, 0.00f, "Combo_2", 0.00f, true);
  km_lunge1.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-1.00f, 0.85f, 0.95f}, {1.00f, 0.85f, 0.95f}, 0.50f, 44.0f, 24.0f));
  km_lunge1.setTrail(true, 0.26f, kKimonoBlade, kKimonoHilt);
  attack_catalog.emplace(AttackID::KimonoLunge1, km_lunge1);

  AttackData km_lunge2(1.19f, 0.22f, 0.00f, "Combo_2", 0.71f, true);
  km_lunge2.addHitBoxDef(HitBoxDefinition::createCapsule(
      {-1.30f, 1.00f, 1.25f}, {1.30f, 1.00f, 1.25f}, 0.55f, 60.0f, 32.0f));
  km_lunge2.setTrail(true, 0.26f, kKimonoBlade, kKimonoHilt);
  attack_catalog.emplace(AttackID::KimonoLunge2, km_lunge2);

  AttackData km_lunge3(0.71f, 0.22f, 1.62f, "Combo_2", 2.12f, true);
  km_lunge3.addHitBoxDef(HitBoxDefinition::createCapsule(
      {1.00f, 0.78f, 1.15f}, {-1.10f, 1.75f, 1.50f}, 0.60f, 68.0f, 40.0f));
  km_lunge3.setTrail(true, 0.26f, kKimonoBlade, kKimonoHilt);
  attack_catalog.emplace(AttackID::KimonoLunge3, km_lunge3);
}
