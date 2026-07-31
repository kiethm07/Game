#include <Entities/PlayerLocomotion.h>
#include <cmath>

bool PlayerLocomotion::update(float dt, bool grounded, float verticalVelocity,
                              float landPlayDuration) {
  if (stagger_timer > 0.0f)
    stagger_timer -= dt;
  if (land_display_timer > 0.0f)
    land_display_timer -= dt;

  bool staggerBegan = false;

  if (!grounded) {
    // Sampled every airborne frame rather than read once on touchdown, because
    // PhysicsManager zeroes vertical velocity in the same pass that snaps the
    // character to the ground — by the time the landing is visible here, the
    // speed it happened at is gone.
    fall_speed = std::fmax(fall_speed, -verticalVelocity);
  } else if (!was_grounded) {
    // Touchdown, this frame only. A gentle one costs nothing: stepping off a
    // low ledge is not an event the character needs to acknowledge, and
    // interrupting the idle for it would make ordinary walking twitch.
    if (fall_speed >= LAND_TRIGGER_SPEED && landPlayDuration > 0.0f) {
      land_display_timer = landPlayDuration;
      // Never longer than the clip it is holding the player through, so a
      // shorter landing animation cannot leave them frozen past its end.
      stagger_timer = std::fmin(LAND_LOCK_DURATION, land_display_timer);
      land_id++;
      staggerBegan = true;
    }
    fall_speed = 0.0f;
  }

  was_grounded = grounded;
  return staggerBegan;
}

ActionGate PlayerLocomotion::gate(const CombatComponent &combat,
                                  bool grounded) const {
  // A stagger is total: until it recovers, nothing may be started. Returned
  // first and whole, so no rule below can accidentally grant something back.
  if (isStaggered())
    return ActionGate{false, false, false, false, false, 0.0f};

  ActionGate g;

  // The combat machine's own commitments. canMove() is false through every
  // attack phase, which is what stops a swing from being steered. canDodge() is
  // repeated here rather than left to startDodge() because the gate has to
  // combine it with the crouch and airborne rules below; startDodge() stays
  // authoritative over whether the dodge is actually latched.
  g.canMove = combat.canMove();
  g.canDodge = combat.canDodge();

  // Left permissive. initiateCombo() and startGuard() each have their own rule
  // about which states may begin one, and duplicating those here would give the
  // two copies somewhere to disagree — startGuard() in particular clears the
  // held-guard flag when it refuses, which never runs if the call is skipped.
  // The gate exists to add restrictions on top of them, and above it has
  // already denied both outright for the duration of a stagger.
  g.canAttack = true;
  g.canGuard = true;

  // No swinging or dashing off the ground. Both are committed, root-driven
  // actions authored against a floor; in mid-air they would either hang
  // motionless or slide the character through the sky.
  if (!grounded) {
    g.canAttack = false;
    g.canDodge = false;
  }

  if (stance == Stance::Crouching) {
    // A dodge out of a crouch would have to stand, roll and re-crouch inside
    // one clip; there is no such animation, and the stance exists to be
    // deliberate.
    g.canDodge = false;
    g.moveSpeedScale = CROUCH_SPEED_SCALE;
  }

  // After the crouch scale rather than before, so a crouched block takes the
  // slower of the two rather than whichever rule happened to run last.
  if (combat.isGuarding())
    g.moveSpeedScale = std::fmin(g.moveSpeedScale, BLOCK_SPEED_SCALE);

  // Nothing to jump from in mid-air, and a player who may not move — mid-swing,
  // mid-dodge — may not leave the ground either.
  g.canJump = grounded && g.canMove;

  return g;
}
