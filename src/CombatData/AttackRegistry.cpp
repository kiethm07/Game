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
}
