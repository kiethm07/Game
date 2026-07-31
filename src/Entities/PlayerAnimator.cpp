#include <Entities/PlayerAnimator.h>
#include <cmath>
#include <raymath.h>

const PlayerAnimator::Machine::Desc *PlayerAnimator::descTable() {
  // Rows are in PlayerAnimState order.
  static const Machine::Desc table[Machine::STATE_COUNT] = {
      //                clip             loop   rate             root   fadeIn
      /* Idle        */ {"Idle", true, 1.0f, false, 0.15f},
      /* Run         */ {"Run", true, RUN_SPEED_SCALE, false, 0.15f},
      /* Jump        */ {"Jump", false, 1.0f, false, 0.08f},
      // Loops, because a fall lasts as long as the drop does rather than as
      // long as the clip. The fade in is the longest in the table: it runs at
      // the apex, between two clips that already agree closely on the pose, so
      // there is nothing to be gained by making the swap crisp.
      /* Fall        */ {"Fall", true, 1.0f, false, 0.20f},
      // Entered 0.30s in, at LAND_CONTACT. Short fade the other way: the impact
      // is the whole point of the clip, and easing into it over a fifth of a
      // second reads as the character sinking to the floor rather than hitting
      // it.
      /* Land        */ {"Land", false, 1.0f, false, 0.06f, LAND_CONTACT},
      /* GuardImpact */ {"Impact", false, 1.0f, false, 0.05f},
      /* Guard       */ {"Block", false, 1.0f, false, 0.10f},
      // Root-driven, so each one's travel is exactly the sideways, forward or
      // backward distance it was authored with. The backstep is authored at
      // 1.67s against the others' 1.0s, half a second of which is the character
      // settling after the travel has finished; time-scaling it brings the
      // recovery — and so the length of the commitment, which is taken from the
      // scaled duration — into line with the other three.
      /* DodgeFwd    */ {"Dodge_Forward", false, 1.0f, true, 0.05f},
      /* DodgeBack   */ {"Dodge_Back", false, 1.5f, true, 0.05f},
      /* DodgeLeft   */ {"Dodge_Left", false, 1.0f, true, 0.05f},
      /* DodgeRight  */ {"Dodge_Right", false, 1.0f, true, 0.05f},
      // Fallback swing, used when an attack names no clip or names one the
      // loaded asset does not contain. Attacks that do name a clip override
      // this in resolve().
      /* Attack      */ {"Slash", false, 1.0f, false, 0.05f},
      /* HitReact    */ {"Impact_2", false, 1.0f, false, 0.05f},
      /* PostureBreak*/ {nullptr, false, 1.0f, false, 0.05f},
      /* Death       */ {nullptr, false, 1.0f, false, 0.10f},
  };
  return table;
}

PlayerAnimator::PlayerAnimator()
    : anim(AssetID::PLAYER_WOLF, descTable()) {}

bool PlayerAnimator::resolveClips(const AssetManager &assets) {
  if (!anim.resolveClips(assets))
    return false;

  const RootMotion::Track &runTrack = anim.track(assets, PlayerAnimState::Run);
  if (runTrack.hasMotion) {
    locomotion_speed = runTrack.authoredSpeed * RUN_SPEED_SCALE;
    TraceLog(LOG_INFO,
             "Player: run clip authored at %.2f u/s, moving at %.2f u/s "
             "(playback %.2fx)",
             runTrack.authoredSpeed, locomotion_speed, RUN_SPEED_SCALE);
  }
  return true;
}

float PlayerAnimator::landPlayDuration(const AssetManager *assets) const {
  if (!assets)
    return 0.0f;
  const RootMotion::Track &track = anim.track(*assets, PlayerAnimState::Land);
  return track.duration > 0.0f ? track.duration - LAND_CONTACT : 0.0f;
}

void PlayerAnimator::queueReaction(bool blocked) {
  queued_reaction =
      blocked ? PlayerAnimState::GuardImpact : PlayerAnimState::HitReact;
}

void PlayerAnimator::updateFlinch(float dt, const AssetManager *assets) {
  if (reaction_timer > 0.0f)
    reaction_timer -= dt;

  const PlayerAnimState queued = queued_reaction;
  queued_reaction = PlayerAnimState::Count;
  if (queued == PlayerAnimState::Count || !assets)
    return;

  if (anim.clipFor(queued) < 0)
    return;

  // The clip's own length is the reaction's length — no separate constant to
  // fall out of sync when the animation is replaced.
  const RootMotion::Track &track = anim.track(*assets, queued);
  if (track.duration <= 0.0f)
    return;

  // The newest hit wins outright rather than queueing behind the one playing:
  // a flinch that is already stale by the time it is shown reads as the
  // character reacting to the wrong hit.
  reaction_state = queued;
  reaction_timer = track.duration;
  reaction_id++;
}

PlayerAnimState PlayerAnimator::dodgeStateFor(Vector3 worldDirection,
                                              float yawDeg) const {
  // Nothing held: a standing dodge is a backstep. It is the reading that costs
  // the player nothing when they meant no direction at all, and the one that
  // opens the most distance from whatever they are backing away from.
  if (worldDirection.x == 0.0f && worldDirection.z == 0.0f)
    return PlayerAnimState::DodgeBack;

  // Into the character's own frame, which is the frame the clips were authored
  // in: +Z is forward and +X is the character's left. That is the same mapping
  // RootMotion::toWorld uses to send their travel back out, so the clip that
  // plays and the direction the player actually moves cannot disagree.
  const float yaw = yawDeg * DEG2RAD;
  const float sinYaw = std::sin(yaw);
  const float cosYaw = std::cos(yaw);
  const float forward = worldDirection.x * sinYaw + worldDirection.z * cosYaw;
  const float left = worldDirection.x * cosYaw - worldDirection.z * sinYaw;

  // The dominant axis wins outright. Four authored clips are not a blend space,
  // so a diagonal has to resolve to one of them; ties go to the forward/back
  // pair, which are the two the player is most likely to have meant when
  // holding a diagonal against a camera that is itself turning.
  if (std::fabs(forward) >= std::fabs(left))
    return forward > 0.0f ? PlayerAnimState::DodgeForward
                          : PlayerAnimState::DodgeBack;
  return left > 0.0f ? PlayerAnimState::DodgeLeft : PlayerAnimState::DodgeRight;
}

float PlayerAnimator::dodgeDuration(const AssetManager &assets,
                                    PlayerAnimState dodge) const {
  if (anim.clipFor(dodge) < 0)
    return 0.0f;

  const RootMotion::Track &track = anim.track(assets, dodge);
  const float rate = anim.desc(dodge).rate;
  return rate > 0.0f ? track.duration / rate : track.duration;
}

PlayerAnimator::Machine::Selection
PlayerAnimator::resolve(const Frame &frame) const {
  // Every rung is guarded on its clip existing, so a state whose animation the
  // loaded asset does not contain — including the three not authored yet —
  // falls through to the next one instead of freezing the character.
  const CombatComponent &combat = *frame.combat;
  const CombatState state = combat.getCurrentState();

  // Highest priority first. A committed state outranks the locomotion states
  // below it, which is what makes an attack or dodge started in mid-air show
  // its own clip rather than the jump.
  // Which of the four dodges was decided when the dodge started; this rung only
  // holds it for the length of the roll.
  if (state == CombatState::Dodging && anim.clipFor(dodge_state) >= 0)
    return anim.select(dodge_state, combat.getActionId());

  if (const AttackData *attack = combat.getActiveAttack()) {
    Machine::Selection selection =
        anim.select(PlayerAnimState::Attack, combat.getActionId());

    // The attack's own clip when it names one the asset contains, otherwise the
    // table's generic swing. Without a fallback the animation would stay on
    // whatever locomotion clip was playing, so the attack would read as the
    // character standing still for its whole duration.
    if (attack->getClipName() && frame.assets) {
      const int named = frame.assets->findAnimation(AssetID::PLAYER_WOLF,
                                                    attack->getClipName());
      if (named >= 0) {
        selection.clip = named;
        selection.rootDriven = attack->usesRootMotion();
      }
    }

    if (selection.clip >= 0)
      return selection;
  }

  // Two clips for one condition, split at the apex. Rising is the jump proper;
  // once the arc turns over it is a fall, and a step off a ledge — which never
  // rises at all — goes straight to the fall without ever showing a launch it
  // did not perform. Falling is the looping half, since a drop lasts as long as
  // the geometry says rather than as long as the clip; the jump is not, so a
  // hang time longer than its clip settles on the last airborne pose instead of
  // launching a second time.
  if (!frame.grounded) {
    const bool rising = frame.verticalVelocity > 0.0f;
    if (!rising && anim.clipFor(PlayerAnimState::Fall) >= 0)
      return anim.select(PlayerAnimState::Fall);
    if (anim.clipFor(PlayerAnimState::Jump) >= 0)
      return anim.select(PlayerAnimState::Jump);
  }

  // Outranks the guard hold for the length of the reaction, and outranks
  // idle/run too, so releasing the button mid-flinch does not cut the hit
  // short. Attacks and dodges still cancel it — they are above this rung, so a
  // hit taken mid-swing does not interrupt the swing.
  if (reaction_timer > 0.0f && anim.clipFor(reaction_state) >= 0)
    return anim.select(reaction_state, reaction_id);

  // One rung for both halves of a guard. The parry window is the front of the
  // same raise the block hold sits on, so they share a clip and a state: the
  // raise runs during the parry, then Blocking picks it up mid-playback with no
  // visible re-trigger. Non-looping, so the clip ends guard-up and holds that
  // pose for as long as the button is down instead of replaying the raise.
  //
  // A guarding player who walks keeps this clip and slides their feet — the
  // pack has no guard-walk cycle wired up yet. Adding one is a table row, a
  // rung above this that tests `frame.moving`, and a per-frame rate so the
  // stride can be scaled to the reduced speed the gate imposes.
  if (combat.isGuarding() && anim.clipFor(PlayerAnimState::Guard) >= 0)
    return anim.select(PlayerAnimState::Guard, combat.getActionId());

  // Below everything that is an action, so a player who lands already swinging,
  // dodging or guarding does that instead of crouching. A stagger stops any of
  // those from being *started*, but one already in flight when the feet touch
  // is cancelled outright by the touchdown, so in practice the rungs above have
  // nothing to return while the stagger runs.
  //
  // Two ways to qualify. While the stagger runs the landing is mandatory, and
  // the moving test is deliberately not consulted: the player is pinned this
  // frame, but the velocity behind that flag is last frame's airborne momentum,
  // which has not been zeroed yet and would otherwise read as movement and skip
  // the clip on the very frame it is supposed to start. Once the stagger lifts,
  // the clip keeps playing out its recovery only for as long as the player
  // stays put; the moment they run, the stride wins.
  if (frame.landingVisible && (frame.staggered || !frame.moving) &&
      anim.clipFor(PlayerAnimState::Land) >= 0)
    return anim.select(PlayerAnimState::Land, frame.landId);

  if (frame.moving && anim.clipFor(PlayerAnimState::Run) >= 0)
    return anim.select(PlayerAnimState::Run);

  return anim.select(PlayerAnimState::Idle);
}

PlayerAnimator::Result PlayerAnimator::update(const Frame &frame, float dt) {
  const Machine::Selection selection = resolve(frame);
  const RootMotion::Track &track = anim.apply(frame.assets, dt, selection);
  return Result{&track, selection.rootDriven};
}
