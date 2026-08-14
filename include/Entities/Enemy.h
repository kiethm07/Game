#pragma once

#include <Components/StealthComponent.h>
#include <Components/AIComponent.h>
#include <Components/CombatComponent.h>
#include <Core/InputManager.h>
#include <Entities/Character.h>
#include <raylib.h>

class Enemy : public Character {
public:
  Enemy(Vector3 start_position, Faction faction = Faction::Enemy);
  virtual ~Enemy() = default;

  virtual void update(const UpdateContext &ctx) override = 0;
  virtual CharacterRenderData getRenderData() const override = 0;
  void drawHPBar(const Camera3D &camera) const;

  bool isBeingExecuted() const override {
    return combat_component.getCurrentState() == CombatState::BeingExecuted;
  }

  float getColliderRadius() const override;
  float getColliderHeight() const override;
  std::vector<HurtBox> getHurtBoxes() const override;
  DamageResult takeDamage(float health_damage, float posture_damage, Character* attacker) override;

  CombatComponent &getCombatComponent() { return combat_component; }
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
  /// Called once per hit that actually landed, with whether the guard caught
  /// it. The hook exists so a subclass can react — a flinch is the whole of it
  /// today — without having to restate takeDamage's parry, block and
  /// posture-break rules, which are the same for every enemy.
  virtual void onDamaged(bool /*blocked*/, bool /*parried*/) {}

  void updateStrafing(const Vector3& velocity, bool enable_strafing = true);

  CombatComponent combat_component;
  AIComponent ai_component;
  StealthComponent stealth_component;
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