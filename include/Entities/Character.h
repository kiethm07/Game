#pragma once

#include "raylib.h"
#include <Components/Faction.h>
#include <Components/HitBox.h>
#include <Components/HurtBox.h>
#include <Components/Stats.h>

class Character {
public:
  Character(Faction faction);
  virtual ~Character() = default;

  virtual void update(float dt) = 0;
  virtual void draw() const;

  Vector3 getPosition() const { return position; }

  Vector3 getRotation() const { return rotation; }

  const Stats &getStats() const { return stats; }
  Faction getFaction() const { return faction; }
  unsigned int getId() const { return id; }

  virtual HurtBox getHurtBox() const = 0;
  virtual std::vector<HitBox> getActiveHitBoxes() const = 0;
  virtual void takeDamage(float health_damage, float posture_damage) = 0;

protected:
  unsigned int id;
  Faction faction;
  Vector3 position;
  Vector3 rotation;
  Stats stats;

private:
  inline static unsigned int next_id = 1;
};