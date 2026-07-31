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

  float getColliderRadius() const override;
  float getColliderHeight() const override;
  std::vector<HurtBox> getHurtBoxes() const override;
  void takeDamage(float health_damage, float posture_damage) override;

  const CombatComponent &getCombatComponent() const { return combat_component; }
  StealthComponent &getStealthComponent() { return stealth_component; }
  const StealthComponent &getStealthComponent() const { return stealth_component; }

protected:
  CombatComponent combat_component;
  AIComponent ai_component;
  StealthComponent stealth_component;
  int moveState = 0;

  float body_height = 2.0f;
  float body_radius = 0.5f;
  Vector3 visual_size = {1.0f, 1.0f, 1.0f};
};