#pragma once

#include <Components/StealthComponent.h>
#include <Components/AIComponent.h>
#include <Components/CombatComponent.h>
#include <Core/InputManager.h>
#include <Entities/Character.h>
#include <Level/EnemySpawn.h>
#include <Rendering/SwordTrail.h>
#include <raylib.h>

class Enemy : public Character {
public:
  /// Build from an authored spawn.
  ///
  /// Takes the spawn rather than a bare position because position, facing and
  /// the post this enemy returns to all come from it and must agree. They used
  /// to be set in two places -- the caller wrote `rotation` after construction
  /// while Swordman's constructor hardcoded `spawn_yaw` to 0 -- so an enemy
  /// authored facing 180 started right and then turned permanently to
  /// world-north the first time it lost the player.
  Enemy(const EnemySpawn &spawn, Faction faction = Faction::Enemy);
  virtual ~Enemy() = default;

  virtual void update(const UpdateContext &ctx) override = 0;
  virtual CharacterRenderData getRenderData() const override = 0;
  void drawHPBar(const Camera3D &camera) const;
  void drawTrail() const override { sword_trail.draw(); }

  bool isBeingExecuted() const override {
    return combat_component.getCurrentState() == CombatState::BeingExecuted;
  }

  float getColliderRadius() const override;
  float getColliderHeight() const override;
  std::vector<HurtBox> getHurtBoxes() const override;

  /// Every hitbox this enemy's current attack has live.
  ///
  /// Implemented here rather than per type: it reads nothing but the combat
  /// state, the transform and the AttackData's own hitbox definitions, so
  /// every subclass's copy was the same forty lines. Left pure on Character
  /// because a non-attacking Character need not have one.
  std::vector<HitBox> getActiveHitBoxes() const override;
  DamageResult takeDamage(float health_damage, float posture_damage, Character* attacker) override;

  CombatComponent &getCombatComponent() { return combat_component; }

  /// Where this enemy was authored, and facing where. Its post: the
  /// unaware behaviour walks back here and settles on this yaw.
  Vector3 getSpawnPosition() const { return spawn_position; }
  float getSpawnYaw() const { return spawn_yaw; }

  /// Which row of EnemyTypes.def this was built from.
  ///
  /// Kept because the concrete class no longer answers the question: a mini
  /// boss and a plain swordman are both a Swordman today, and the phase gate
  /// has to tell them apart. Set from the spawn, so it cannot disagree with
  /// what the level authored.
  EnemyType getType() const { return type; }
  const CombatComponent &getCombatComponent() const { return combat_component; }
  StealthComponent &getStealthComponent() { return stealth_component; }
  const StealthComponent &getStealthComponent() const { return stealth_component; }

  bool hasDroppedLoot() const { return dropped_loot; }
  void setDroppedLoot(bool dropped) { dropped_loot = dropped; }

  bool isKilledByStealth() const { return killed_by_stealth; }
  void setKilledByStealth(bool killed) { killed_by_stealth = killed; }
  
  float getDissolveTimer() const { return dissolve_timer; }
  void addDissolveTimer(float dt) { dissolve_timer += dt; }
  bool isFullyDissolved() const { return dissolve_timer >= 2.0f; }

  bool isModelUnloaded() const { return model_unloaded; }
  void setModelUnloaded(bool unloaded) { model_unloaded = unloaded; }

  int getDecayType() const { return static_cast<int>(decay_type); }
  void setDecayType(DecayType type) { decay_type = type; }

  bool isStrafing() const { return is_strafing; }
  Vector3 getLocalMoveDir() const { return localMoveDir; }

protected:
  /// Follow `current_path`, one waypoint at a time, turning to face the way.
  ///
  /// Returns true on arrival (the path is spent), false while still walking --
  /// deliberately a bool rather than a BT::NodeState, so that AI/BehaviorTree.h
  /// does not become a dependency of this base for the sake of one enum. Each
  /// subclass's tree maps it in its own action.
  bool moveAlongPath(float speed);

  /// Cut `path` short where it would walk into a smoke cloud.
  void truncatePathBySmoke(std::vector<Vector3> &path);

  /// The route this enemy is currently walking, and how long until it is
  /// recomputed. Here rather than in a subclass because moveAlongPath is.
  std::vector<Vector3> current_path;
  float path_recalc_timer = 0.0f;

  /// This frame's context, latched at the top of update() so the behaviour
  /// tree's nodes -- which take no arguments -- can reach it. Only ever valid
  /// inside an update() call.
  const UpdateContext *current_ctx = nullptr;

  /// Where this enemy was authored, and the facing it returns to.
  ///
  /// Lives here rather than in a subclass because "walk back to your post" is
  /// an Enemy-level idea and because setting it anywhere but the constructor is
  /// how it came adrift from `rotation` in the first place.
  EnemyType type = EnemyType::Swordman;
  Vector3 spawn_position{0.0f, 0.0f, 0.0f};
  float spawn_yaw = 0.0f;

  /// Called once per hit that actually landed, with whether the guard caught
  /// it. The hook exists so a subclass can react — a flinch is the whole of it
  /// today — without having to restate takeDamage's parry, block and
  /// posture-break rules, which are the same for every enemy.
  virtual void onDamaged(bool /*blocked*/, bool /*parried*/) {}

  void updateStrafing(const Vector3& velocity, bool enable_strafing = true);
  void updateCombatCircling(const UpdateContext& ctx, Vector3 target_pos, float move_speed, float rot_speed = 18.0f);

  float circle_direction = 1.0f;
  float circle_timer = 0.0f;
  float preferred_distance_min = 3.0f;
  float preferred_distance_max = 4.0f;
  bool in_direct_combat = false;

  CombatComponent combat_component;
  AIComponent ai_component;
  StealthComponent stealth_component;
  SwordTrail sword_trail;
  int moveState = 0;
  bool dropped_loot = false;
  
  bool is_strafing = false;
  Vector3 localMoveDir = {0.0f, 0.0f, 0.0f};
  
  bool killed_by_stealth = false;
  float dissolve_timer = 0.0f;
  bool model_unloaded = false;
  DecayType decay_type = DecayType::ASH_DECAY;

  float body_height = 2.0f;
  float body_radius = 0.5f;
  Vector3 visual_size = {1.0f, 1.0f, 1.0f};
};