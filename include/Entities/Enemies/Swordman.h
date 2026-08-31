#pragma once

#include <CombatData/Combo.h>
#include <Entities/Enemies/SwordmanAnimator.h>
#include <Entities/Enemy.h>
#include <cstddef>
#include <vector>

class Swordman : public Enemy {
public:
  /// The AssetID is a parameter, not a constant, for the same reason
  /// SwordmanAnimator's is: MiniBoss and FinalBoss are built as a Swordman
  /// (EnemyFactory.cpp) and passing a different asset here is how a boss
  /// draws its own model without a class of its own. Default matches
  /// SwordmanAnimator's -- the ashigaru borrowing the player's asset.
  Swordman(const EnemySpawn &spawn, AssetID asset = AssetID::ENEMY_ASHIGARU);

  /// Ceiling on the speed an attack's authored travel may produce, in m/s.
  /// Only a dt spike can reach it; the final boss's leap peaks near 6.
  static constexpr float MAX_ATTACK_ROOT_SPEED = 20.0f;
  ~Swordman() override = default;

  void update(const UpdateContext &ctx) override;
  CharacterRenderData getRenderData() const override;

protected:
  /// Flinches on a hit that connected. Enemy::takeDamage decides whether one
  /// did and whether the guard caught it; all that is left here is showing it.
  void onDamaged(bool blocked, bool parried) override;

private:
  /// Drives the character from the playing attack clip's own travel, for
  /// attacks whose AttackData asks for it. Enemies were in place by
  /// construction before this: the flag existed on AttackData but only the
  /// player ever read it.
  ///
  /// Also the one place that hands over to applyAttackAdvance, because the two
  /// are the same decision -- whether the behaviour tree or the attack owns
  /// this frame's velocity -- and splitting it would leave two functions both
  /// believing they had to release it afterwards.
  void applyAttackRootMotion(float dt);

  /// Walks the character onto its target while an attack that asks for one is
  /// swinging. The counterpart to applyAttackRootMotion for attacks whose clip
  /// carries no travel and whose length would otherwise let a player simply
  /// leave -- see AttackData::getAdvanceSpeed.
  void applyAttackAdvance(float dt, const AttackData &attack);

  /// Whether the running attack is between its first hit window and its last,
  /// which is the stretch a chase belongs in -- not the wind-up before it, and
  /// not the recovery after.
  bool attackIsSwinging() const;

  /// Whether the attack wrote the horizontal velocity last frame -- from the
  /// clip's travel or from a chase -- and therefore owes a release when it
  /// stops.
  bool root_motion_driving = false;

  /// The attacks this enemy throws, in the order it throws them, wrapping at
  /// the end. One entry for an ashigaru -- it has exactly one swing, so the
  /// rotation is a rotation of one and behaves as it always did. Three for the
  /// mini boss: a single cut, a two-hit combo, a three-hit combo, and round
  /// again, which is the pattern that was asked for.
  ///
  /// Filled once, in the constructor, and never touched afterwards. That is
  /// load-bearing: CombatComponent::initiateCombo keeps a POINTER to the Combo
  /// it was handed for as long as the attack runs, so growing this vector
  /// mid-fight would leave the running attack reading freed memory.
  std::vector<Combo> attack_pattern;

  /// Where the rotation is up to. Advanced when an attack is launched rather
  /// than when it finishes, so an attack cut short by a posture break does not
  /// make the next one repeat it.
  std::size_t next_attack = 0;

  SwordmanAnimator animator;

  // BODY_HEIGHT, BODY_RADIUS, ATTACK_REACH, ATTACK_RADIUS and ROTATION_SPEED
  // used to sit here and were referenced by nothing -- the collider actually
  // comes from Enemy's body_height/body_radius. They are gone because five
  // dead constants are exactly what a second enemy type would copy and then
  // wonder why editing does nothing.

  /// The two gaits, split out of the single MOVEMENT_SPEED that used to serve
  /// both. Which one is in use is the whole difference between circling on
  /// cooldown and closing in: everything the enemy does at a stroll -- circling,
  /// investigating, walking back to its post -- is a fraction of `walk_speed`,
  /// and every approach runs at `run_speed`.
  ///
  /// They also decide which CLIP plays, which is why one number could not do
  /// the job. SwordmanAnimator picks Run over Walk above RUN_SPEED_FACTOR times
  /// the walk clip's own authored speed and then time-scales whichever it
  /// picked to the speed actually being travelled -- so a speed that lands just
  /// over the threshold selects the run and plays it in slow motion. Both
  /// values are set per enemy against its own pack's authored speeds; see the
  /// constructor.
  float walk_speed = 2.0f;
  float run_speed = 2.0f;

  /// The SoundSensor's radius, and the gait switch below. One constant because
  /// they are the same circle: the blue ring the sensor debug draws is exactly
  /// the line this enemy starts running outside of, so moving one moves both.
  static constexpr float HEARING_RADIUS = 6.0f;

  /// Where an approach changes gait: run outside this, walk inside it. 0
  /// disables the switch and approaches always run, which is what the mini
  /// boss does -- its 6.5 charge is tuned to close relentlessly and is not a
  /// two-gait approach.
  float gait_switch_distance = HEARING_RADIUS;

  /// Latched so the gait switch has hysteresis. Without it a player sitting on
  /// the boundary flips the gait -- and therefore the CLIP -- every frame, and
  /// the 0.15 s blend never resolves.
  bool chase_running = false;

  /// walk_speed or run_speed for an approach at `distance`, latching
  /// `chase_running` across the GAIT_SWITCH_HYSTERESIS band.
  float chaseSpeed(float distance);

  /// Half-width of the band around `gait_switch_distance` the switch must be
  /// crossed by before the gait actually changes.
  static constexpr float GAIT_SWITCH_HYSTERESIS = 0.5f;

  void setupBehaviorTree();

  // spawn_position and spawn_yaw are Enemy's now, set from the spawn in one
  // place so they cannot disagree with `rotation`.
  float attack_cooldown_timer = 0.0f;
  float move_cooldown_timer = 0.0f;
  float investigation_timer = 0.0f;
};