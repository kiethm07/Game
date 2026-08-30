#include <Entities/Enemies/SwordmanAnimator.h>

const SwordmanAnimator::Machine::Desc *SwordmanAnimator::descTable(AssetID asset) {
  // Rows are in SwordmanAnimState order.
  //
  // Two packs, because the two enemies no longer share clip NAMES. The ashigaru
  // borrows the player's asset whole; the miniboss carries a greatsword pack
  // built for it by tools/build_miniboss_pack.py, whose clips are named after
  // the states they serve. A state a pack has no clip for resolves to -1 and
  // the ladder in resolve() falls past it, which is how the ashigaru gets by
  // with no GuardWalk and how either would get by with no Fall.
  static const Machine::Desc ashigaru[Machine::STATE_COUNT] = {
      //                clip          loop   rate   root   fadeIn
      /* Idle        */ {"Idle", true, 1.0f, false, 0.15f},
      /* Walk        */ {"Walk", true, 1.0f, false, 0.15f},
      /* Run         */ {"Run", true, 1.0f, false, 0.15f},
      /* StrafeFwd   */ {"Walk", true, 1.0f, false, 0.15f},
      /* StrafeBack  */ {"Walk_2", true, 1.0f, false, 0.15f},
      /* StrafeLeft  */ {"Strafe_2", true, 1.0f, false, 0.15f},
      /* StrafeRight */ {"Strafe", true, 1.0f, false, 0.15f},
      /* Fall        */ {"Fall", true, 1.0f, false, 0.15f},
      /* GuardImpact */ {"Impact", false, 1.0f, false, 0.05f},
      /* HitReact    */ {"Impact_2", false, 1.0f, false, 0.05f},
      // Fallback swing, used when an attack names no clip or names one the
      // asset does not contain. Attacks that do name a clip override this in
      // resolve(), which is how the swordman's combo plays the same authored
      // swings the player's does.
      /* Attack      */ {"Slash", false, 1.0f, false, 0.05f},
      /* PostureBreak*/ {"FallToKneel", false, 1.0f, false, 0.08f},
      /* Death       */ {"Death", false, 1.0f, false, 0.10f},
      /* Guard       */ {"Block", false, 1.0f, false, 0.05f},
      /* GuardWalk   */ {"BlockWalk", true, 1.0f, false, 0.10f},
  };

  // The greatsword pack. Every name here is one this pack defines; the two it
  // kept from the player's set (Fall, PostureBreak) were renamed on the way in
  // so nothing in this table has to know where a clip came from.
  static const Machine::Desc miniboss[Machine::STATE_COUNT] = {
      //                clip          loop   rate   root   fadeIn
      /* Idle        */ {"Idle", true, 1.0f, false, 0.15f},
      /* Walk        */ {"Walk", true, 1.0f, false, 0.15f},
      /* Run         */ {"Run", true, 1.0f, false, 0.15f},
      /* StrafeFwd   */ {"Walk", true, 1.0f, false, 0.15f},
      /* StrafeBack  */ {"StrafeBack", true, 1.0f, false, 0.15f},
      /* StrafeLeft  */ {"StrafeLeft", true, 1.0f, false, 0.15f},
      /* StrafeRight */ {"StrafeRight", true, 1.0f, false, 0.15f},
      /* Fall        */ {"Fall", true, 1.0f, false, 0.15f},
      // Both guard clips are built by tools/make_miniboss_guard.py, so the
      // flinch and the hold share one pose and the recoil reads as the same
      // guard being driven back rather than as a different one.
      /* GuardImpact */ {"GuardImpact", false, 1.0f, false, 0.05f},
      /* HitReact    */ {"HitReact", false, 1.0f, false, 0.05f},
      /* Attack      */ {"Attack", false, 1.0f, false, 0.05f},
      /* PostureBreak*/ {"PostureBreak", false, 1.0f, false, 0.08f},
      /* Death       */ {"Death", false, 1.0f, false, 0.10f},
      /* Guard       */ {"Guard", false, 1.0f, false, 0.08f},
      /* GuardWalk   */ {"GuardWalk", true, 1.0f, false, 0.10f},
  };

  return asset == AssetID::ENEMY_MINIBOSS ? miniboss : ashigaru;
}

SwordmanAnimator::SwordmanAnimator(AssetID asset)
    : asset_id(asset), anim(asset, descTable(asset)) {}

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
  if (blocked) {
    queued_reaction = SwordmanAnimState::GuardImpact;
  } else {
    queued_reaction = SwordmanAnimState::HitReact;
  }
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
      const int named = frame.assets->findAnimation(asset_id,
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

  // Airborne, above the locomotion rungs: a walk cycle played in mid-air reads
  // as running on nothing. Below the swing and the flinch, both of which are
  // committed actions that outlive a step off a ledge.
  if (!frame.grounded && anim.clipFor(SwordmanAnimState::Fall) >= 0)
    return anim.select(SwordmanAnimState::Fall);

  if (combat.isGuarding()) {
    // Carried when moving, held when not -- and GuardWalk is optional, so a
    // pack without it keeps the standing guard rather than dropping the guard
    // entirely and walking with the weapon down.
    const SwordmanAnimState guard =
        (frame.moving && anim.clipFor(SwordmanAnimState::GuardWalk) >= 0)
            ? SwordmanAnimState::GuardWalk
            : SwordmanAnimState::Guard;
    if (anim.clipFor(guard) >= 0)
      return anim.select(guard, combat.getActionId());
  }

  if (frame.moving) {
    SwordmanAnimState cycle = SwordmanAnimState::Walk;
    if (frame.strafing) {
      float z_weight = std::abs(frame.localMoveDir.z);
      float x_weight = std::abs(frame.localMoveDir.x);

      SwordmanAnimState current = anim.activeState();
      // Hysteresis: only add bias to the axis comparison, never distorting direction signs
      if (current == SwordmanAnimState::StrafeForward || current == SwordmanAnimState::StrafeBack) {
        z_weight += 0.35f;
      } else if (current == SwordmanAnimState::StrafeLeft || current == SwordmanAnimState::StrafeRight) {
        x_weight += 0.35f;
      }

      if (z_weight >= x_weight) {
        if (frame.localMoveDir.z >= 0.0f) {
          cycle = SwordmanAnimState::StrafeForward;
        } else {
          cycle = SwordmanAnimState::StrafeBack;
        }
      } else {
        if (frame.localMoveDir.x >= 0.0f) {
          cycle = SwordmanAnimState::StrafeLeft;
        } else {
          cycle = SwordmanAnimState::StrafeRight;
        }
      }
    }

    // Not strafing: a straight approach fast enough to outrun the walk clip
    // gets the run instead. Measured against that clip's OWN authored speed via
    // locomotion_speed, so this does not need to know what the pack's walk was
    // authored at -- the greatsword walk is 1.07 m/s and its run 4.03, and the
    // player pack's differ again.
    else if (locomotion_speed > 0.0f &&
             frame.speed > locomotion_speed * RUN_SPEED_FACTOR &&
             anim.clipFor(SwordmanAnimState::Run) >= 0) {
      cycle = SwordmanAnimState::Run;
    }

    if (anim.clipFor(cycle) < 0) {
      cycle = SwordmanAnimState::Walk;
    }

    if (anim.clipFor(cycle) >= 0) {
      Machine::Selection selection = anim.select(cycle);
      if (frame.assets != nullptr && frame.speed > 0.0f) {
        const RootMotion::Track& track = anim.track(*frame.assets, cycle);
        if (track.hasMotion && track.authoredSpeed > 0.0f) {
          selection.rate = frame.speed / track.authoredSpeed;
        }
      }
      return selection;
    }
  }

  return anim.select(SwordmanAnimState::Idle);
}

void SwordmanAnimator::update(const Frame &frame, float dt) {
  anim.apply(frame.assets, dt, resolve(frame));
}
