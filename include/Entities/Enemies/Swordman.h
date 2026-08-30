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
  ~Swordman() override = default;

  void update(const UpdateContext &ctx) override;
  CharacterRenderData getRenderData() const override;

protected:
  /// Flinches on a hit that connected. Enemy::takeDamage decides whether one
  /// did and whether the guard caught it; all that is left here is showing it.
  void onDamaged(bool blocked, bool parried) override;

private:
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
  void setupBehaviorTree();

  // spawn_position and spawn_yaw are Enemy's now, set from the spawn in one
  // place so they cannot disagree with `rotation`.
  float attack_cooldown_timer = 0.0f;
  float move_cooldown_timer = 0.0f;
  float investigation_timer = 0.0f;
};