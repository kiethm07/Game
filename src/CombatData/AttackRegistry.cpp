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
    attack_catalog.emplace(AttackID::PlayerLight1,
                           AttackData(0.55f, 0.20f, 0.76f, "Slash", false));

    // Slash_3: 1.60s authored, small step in and out.
    attack_catalog.emplace(AttackID::PlayerLight2,
                           AttackData(0.55f, 0.20f, 0.83f, "Slash_3", true));

    // Attack_2: 1.33s authored, 2.7-unit forward lunge.
    attack_catalog.emplace(AttackID::PlayerHeavyFinisher,
                           AttackData(0.45f, 0.18f, 0.68f, "Attack_2", true));
}
