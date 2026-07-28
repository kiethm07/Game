#pragma once

#include <Components/AnimationComponent.h>
#include <Components/CombatComponent.h>
#include <Components/MovementComponent.h>
#include <Core/InputManager.h>
#include <Entities/Character.h>
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
  void takeDamage(float health_damage, float posture_damage) override;

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

  CombatComponent combat_component;
  MovementComponent movement_component;
  AnimationComponent animation;
  Combo combo;

  /// How fast the player travels relative to the run clip's authored speed.
  /// 1.0 plays the clip at its natural pace; higher time-scales the clip to
  /// match so the feet keep up instead of sliding.
  static constexpr float RUN_SPEED_SCALE = 1.6f;

  /// Clip indices, resolved by name on the first update. -1 until then, and
  /// for clips the loaded asset does not contain.
  struct Clips {
    int idle = -1;
    int run = -1;
    int dodge = -1;
    int jump = -1;
    bool resolved = false;
  };
  Clips clips;

  void resolveClips(const AssetManager &assets);
  void updateLocomotion(const UpdateContext &ctx, Vector3 moveDirection);
  void updateCommittedState(const UpdateContext &ctx);
  void applyRootMotion(const RootMotion::Track &track, float dt);

  Vector3 calculateCameraRelativeDirection(Vector3 camForward,
                                           Vector3 camRight) const;
  void handleCombatAndUtilityInputs(const UpdateContext &ctx);
};