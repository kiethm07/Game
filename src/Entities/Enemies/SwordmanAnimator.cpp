#include <Entities/Enemies/SwordmanAnimator.h>

const SwordmanAnimator::Machine::Desc *SwordmanAnimator::descTable() {
  // Rows are in SwordmanAnimState order. Every clip named here belongs to the
  // player's asset, which the ashigaru borrows whole.
  static const Machine::Desc table[Machine::STATE_COUNT] = {
      //                clip          loop   rate   root   fadeIn
      /* Idle        */ {"Idle", true, 1.0f, false, 0.15f},
      // Rate overridden per frame once something moves an enemy, the same way
      /* Walk        */ {"Walk", true, 1.0f, false, 0.15f},
      /* StrafeFwd   */ {"Walk", true, 1.0f, false, 0.15f},
      /* StrafeBack  */ {"Walk_2", true, 1.0f, false, 0.15f},
      /* StrafeLeft  */ {"Strafe_2", true, 1.0f, false, 0.15f},
      /* StrafeRight */ {"Strafe", true, 1.0f, false, 0.15f},
      /* GuardImpact */ {"Impact", false, 1.0f, false, 0.05f},
      /* HitReact    */ {"Impact_2", false, 1.0f, false, 0.05f},
      // Fallback swing, used when an attack names no clip or names one the
      // asset does not contain. Attacks that do name a clip override this in
      // resolve(), which is how the swordman's combo plays the same authored
      // swings the player's does.
      /* Attack      */ {"Slash", false, 1.0f, false, 0.05f},
      /* PostureBreak*/ {"FallToKneel", false, 1.0f, false, 0.08f},
      /* Death       */ {"Death", false, 1.0f, false, 0.10f},
      /* Parry       */ {"Block", false, 1.0f, false, 0.05f},
  };
  return table;
}

SwordmanAnimator::SwordmanAnimator()
    : anim(AssetID::ENEMY_ASHIGARU, descTable()) {}

bool SwordmanAnimator::resolveClips(const AssetManager &assets) {
  if (!anim.resolveClips(assets))
    return false;

  const RootMotion::Track &walkTrack =
      anim.track(assets, SwordmanAnimState::Walk);
  if (walkTrack.hasMotion) {
    locomotion_speed = walkTrack.authoredSpeed;
    TraceLog(LOG_INFO, "Swordman: walk clip authored at %.2f u/s",
             locomotion_speed);
  }
  return true;
}

void SwordmanAnimator::queueReaction(bool blocked) {
  queued_reaction =
      blocked ? SwordmanAnimState::GuardImpact : SwordmanAnimState::HitReact;
}

void SwordmanAnimator::updateFlinch(float dt, const AssetManager *assets) {
  if (reaction_timer > 0.0f)
    reaction_timer -= dt;

  const SwordmanAnimState queued = queued_reaction;
  queued_reaction = SwordmanAnimState::Count;
  if (queued == SwordmanAnimState::Count || !assets)
    return;

  if (anim.clipFor(queued) < 0)
    return;

  // The clip's own length is the reaction's length — no separate constant to
  // fall out of sync when the animation is replaced.
  const RootMotion::Track &track = anim.track(*assets, queued);
  if (track.duration <= 0.0f)
    return;

  // The newest hit wins outright rather than queueing behind the one playing.
  reaction_state = queued;
  reaction_timer = track.duration;
  reaction_id++;
}

SwordmanAnimator::Machine::Selection
SwordmanAnimator::resolve(const Frame &frame) const {
  // Every rung is guarded on its clip existing, so a state whose animation the
  // loaded asset does not contain falls through to the next one instead of
  // freezing the character.

  // Above everything: a corpse does not flinch, swing or walk. Non-looping, so
  // it plays once and holds its last pose for as long as the body is up.
  if (frame.dead && anim.clipFor(SwordmanAnimState::Death) >= 0)
    return anim.select(SwordmanAnimState::Death);

  const CombatComponent &combat = *frame.combat;

  // Directly below death, above everything the enemy could otherwise be doing.
  // breakPosture() has already dropped the combo and the guard, so no swing can
  // be running here — but the flinch from the hit that broke the guard is still
  // ticking, and this has to win over it. Variant is the action id bumped by
  // breakPosture(), so a second break restarts the clip rather than resuming a
  // stale one.
  if (combat.getCurrentState() == CombatState::PostureBroken &&
      anim.clipFor(SwordmanAnimState::PostureBreak) >= 0)
    return anim.select(SwordmanAnimState::PostureBreak, combat.getActionId());

  if (const AttackData *attack = combat.getActiveAttack()) {
    Machine::Selection selection =
        anim.select(SwordmanAnimState::Attack, combat.getActionId());

    // The attack's own clip when it names one the asset contains, otherwise the
    // table's generic swing. Root motion is deliberately dropped even where the
    // attack asks for it: nothing feeds an enemy's clip travel back into its
    // position, so claiming the clip drives movement would only leave the mesh
    // sliding off its capsule.
    if (attack->getClipName() && frame.assets) {
      const int named = frame.assets->findAnimation(AssetID::ENEMY_ASHIGARU,
                                                    attack->getClipName());
      if (named >= 0)
        selection.clip = named;
    }

    if (selection.clip >= 0)
      return selection;
  }

  // Below the swing, so a hit taken mid-attack does not interrupt it — the same
  // ordering the player's ladder uses.
  if (reaction_timer > 0.0f && anim.clipFor(reaction_state) >= 0)
    return anim.select(reaction_state, reaction_id);

  if (combat.isGuarding() && anim.clipFor(SwordmanAnimState::Parry) >= 0)
    return anim.select(SwordmanAnimState::Parry, combat.getActionId());

  if (frame.moving) {
      if (frame.strafing) {
          SwordmanAnimState cycle = SwordmanAnimState::Walk;
          float z_weight = std::abs(frame.localMoveDir.z);
          float x_weight = std::abs(frame.localMoveDir.x);
          
          float z_dir = frame.localMoveDir.z;
          float x_dir = frame.localMoveDir.x;
          
          SwordmanAnimState current = anim.activeState();
          // Hysteresis: strongly prefer the axis AND direction we are already playing to prevent flip-flopping
          if (current == SwordmanAnimState::StrafeForward) {
              z_weight += 0.35f; z_dir += 0.35f;
          } else if (current == SwordmanAnimState::StrafeBack) {
              z_weight += 0.35f; z_dir -= 0.35f;
          } else if (current == SwordmanAnimState::StrafeLeft) {
              x_weight += 0.35f; x_dir += 0.35f;
          } else if (current == SwordmanAnimState::StrafeRight) {
              x_weight += 0.35f; x_dir -= 0.35f;
          }
          
          if (z_weight >= x_weight) {
              cycle = z_dir > 0.0f ? SwordmanAnimState::StrafeForward : SwordmanAnimState::StrafeBack;
          } else {
              cycle = x_dir > 0.0f ? SwordmanAnimState::StrafeLeft : SwordmanAnimState::StrafeRight;
          }
          if (anim.clipFor(cycle) >= 0) return anim.select(cycle);
      }
      if (anim.clipFor(SwordmanAnimState::Walk) >= 0)
          return anim.select(SwordmanAnimState::Walk);
  }

  return anim.select(SwordmanAnimState::Idle);
}

void SwordmanAnimator::update(const Frame &frame, float dt) {
  anim.apply(frame.assets, dt, resolve(frame));
}
