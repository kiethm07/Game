#pragma once

#include "raylib.h"
#include <Components/Faction.h>
#include <Components/HitBox.h>
#include <Components/HurtBox.h>
#include <Components/Stats.h>
#include <Rendering/RenderData.h>
#include <vector>

/// Everything an entity needs to advance one tick. Passed through the virtual
/// update() so subclasses share a single polymorphic entry point.
struct UpdateContext {
  float dt = 0.0f;
  Vector3 camForward = {0.0f, 0.0f, 1.0f};
  Vector3 camRight = {1.0f, 0.0f, 0.0f};
  Vector3 playerPos = {0.0f, 0.0f, 0.0f};
};

class Character {
public:
  Character(Faction faction);
  virtual ~Character() = default;

  virtual void update(const UpdateContext &ctx) = 0;
  virtual CharacterRenderData getRenderData() const = 0;
  virtual void draw() const;

  Vector3 getPosition() const { return position; }

  void setPosition(const Vector3 &new_position) { position = new_position; }

  Vector3 getRotation() const { return rotation; }

  const Stats &getStats() const { return stats; }
  Faction getFaction() const { return faction; }
  unsigned int getId() const { return id; }

  float getVerticalVelocity() const { return vertical_velocity; }
  void setVerticalVelocity(float velocity) { vertical_velocity = velocity; }

  bool isGrounded() const { return is_grounded; }
  void setGrounded(bool grounded) { is_grounded = grounded; }

  // Top-Y of the surface the character was last resting on. Used by the physics
  // step to keep it on the same layer across frames (anti-flicker at seams).
  float getGroundReferenceY() const { return ground_reference_y; }
  void setGroundReferenceY(float y) { ground_reference_y = y; }

  virtual float getColliderRadius() const = 0;
  virtual float getColliderHeight() const = 0;

  BoundingBox getBoundingBox() const {
    float radius = getColliderRadius();
    float height = getColliderHeight();
    Vector3 min_corner = {position.x - radius, position.y, position.z - radius};
    Vector3 max_corner = {position.x + radius, position.y + height,
                          position.z + radius};
    BoundingBox box;
    box.min = min_corner;
    box.max = max_corner;
    return box;
  }

  virtual std::vector<HurtBox> getHurtBoxes() const = 0;
  virtual std::vector<HitBox> getActiveHitBoxes() const = 0;
  virtual void takeDamage(float health_damage, float posture_damage) = 0;

protected:
  unsigned int id;
  Faction faction;
  Vector3 position;
  Vector3 rotation;
  Stats stats;

  float vertical_velocity = 0.0f;
  bool is_grounded = true;
  float ground_reference_y = 0.0f;

private:
  inline static unsigned int next_id = 1;
};