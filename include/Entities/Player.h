#pragma once

#include <Components/CombatComponent.h>
#include <Components/MovementComponent.h>
#include <Core/InputManager.h>
#include <Entities/Character.h>
#include <Entities/PlayerAnimator.h>
#include <Entities/PlayerLocomotion.h>
#include <Rendering/RootMotion.h>

class Player : public Character {
public:
  Player(const InputManager &input_manager);
  ~Player() = default;

  void update(const UpdateContext &ctx) override;
  CharacterRenderData getRenderData() const override;

  void drawHPBar2D() const;

  float getColliderRadius() const override;
  float getColliderHeight() const override;
  std::vector<HurtBox> getHurtBoxes() const override;
  std::vector<HitBox> getActiveHitBoxes() const override;
  DamageResult takeDamage(float health_damage, float posture_damage, Character* attacker) override;

  bool isCrouching() const override { return locomotion.getStance() == Stance::Crouching; }
  bool isInSmoke() const { return in_smoke_flag; }

  CombatComponent& getCombatComponent() { return combat_component; }

  /// Plays the deathblow swing. Interrupts first because the damage has already
  /// been dealt by the time this is called: initiateCombo() only takes from Idle
  /// or from the recovery of the same combo, so a takedown pressed out of a
  /// guard or a dodge would otherwise kill the target with no animation at all.
  void performTakedown() {
      locomotion.resetFallSpeed(); // Prevent landing stagger from cancelling the takedown
      combat_component.interrupt();
      combat_component.initiateCombo(execution_combo);
  }

  /// Whether the player is dashing — mid-dodge or sprinting. Asked by the
  /// camera, which frames a dash wider than a walk. One question with one
  /// answer, rather than the camera reassembling it from a gait and a combat
  /// state it would have to be handed anyway.
  ///
  /// The two halves are asked separately because the gate deliberately reports
  /// Walking *during* a dodge — sprinting is what the key means once the dash
  /// it began has finished — so the dodge has to be tested on its own.
  bool isDashing() const;

  /// Whether the deathblow swing is on screen right now. Asked by the camera,
  /// which holds its shot for exactly as long as this is true.
  ///
  /// Read from the attack actually running rather than from a timer started
  /// alongside it: the two cannot drift, and an execution cut short — a landing
  /// stagger interrupts one — drops the shot on the same frame it drops the
  /// animation, instead of leaving the camera composed on a swing that stopped.
  bool isExecuting() const override;

private:
  const InputManager &input_manager;

  const float BODY_HEIGHT = 1.8f;
  const float BODY_RADIUS = 0.5f;
  const float ATTACK_REACH = 1.2f;
  const float ATTACK_RADIUS = 0.8f;

  /// Ceiling on root-motion-derived velocity. Root motion divides a frame's
  /// travel by dt, so a dt spike (breakpoint, window drag) would otherwise
  /// produce an unbounded velocity and tunnel the character through geometry.
  static constexpr float MAX_ROOT_MOTION_SPEED = 40.0f;

  /// Take-off speed in units/second. Against PhysicsManager's 9.81 gravity this
  /// apexes at v^2/2g ~= 1.27 units (about two thirds of body height) after
  /// ~0.51s, for a ~1.0s round trip — matching the Jump_2 clip's 1.00s, so the
  /// landing pose reads correctly.
  static constexpr float JUMP_SPEED = 5.0f;

  /// How hard the player may steer while airborne, in units/second^2. Expressed
  /// as an acceleration rather than a blend factor so it stays frame-rate
  /// independent: at roughly 2x ground speed it takes ~0.5s to fully redirect a
  /// jump. Full instant control in the air feels weightless; none at all makes
  /// a mistimed jump unrecoverable.
  static constexpr float AIR_ACCELERATION = 13.0f;

  bool in_smoke_flag = false;

  CombatComponent combat_component;
  MovementComponent movement_component;
  Combo combo;

  /// The deathblow, held as a one-attack combo of its own so it never chains
  /// into the light combo and the light combo never chains into it.
  Combo execution_combo;

  /// Stance and landing recovery, and the one function that decides what the
  /// player is allowed to do with them.
  PlayerLocomotion locomotion;

  /// The gait the gate settled on this frame. Kept because the gate itself is a
  /// local inside update() and the camera is ticked afterwards: without it the
  /// camera would have to re-derive the gait from raw input, which is a second
  /// place for the rule to live and drift.
  Gait gait = Gait::Walking;

  /// Which clip plays, and the clock that drives it.
  PlayerAnimator animator;

  void updateLocomotionVelocity(const UpdateContext &ctx, Vector3 moveDirection,
                                float speedScale);
  void applyRootMotion(const RootMotion::Track &track, float dt);

  Vector3 calculateCameraRelativeDirection(Vector3 camForward,
                                           Vector3 camRight) const;
  void handleCombatAndUtilityInputs(const UpdateContext &ctx,
                                    const ActionGate &gate);
};
