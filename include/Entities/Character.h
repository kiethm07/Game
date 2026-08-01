#pragma once

#include "raylib.h"
#include <Components/Faction.h>
#include <Components/HitBox.h>
#include <Components/HurtBox.h>
#include <Components/Stats.h>
#include <Rendering/AssetManager.h>
#include <Rendering/RenderData.h>
#include <vector>

/// Everything an entity needs to advance one tick. Passed through the virtual
/// update() so subclasses share a single polymorphic entry point.
struct UpdateContext {
  float dt = 0.0f;
  Vector3 camForward = {0.0f, 0.0f, 1.0f};
  Vector3 camRight = {1.0f, 0.0f, 0.0f};
  Vector3 playerPos = {0.0f, 0.0f, 0.0f};

  /// Read-only access to loaded assets, for the root motion tracks that drive
  /// displacing states (attacks, dodges) and set locomotion speed. Never null
  /// in the normal update path.
  const AssetManager *assets = nullptr;
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

  // Desired horizontal (x,z) velocity produced by the movement layer and
  // integrated by PhysicsManager. PhysicsManager is the sole writer of position.
  Vector3 getHorizontalVelocity() const { return horizontal_velocity; }
  void setHorizontalVelocity(const Vector3 &velocity) {
    horizontal_velocity = velocity;
  }

  bool isGrounded() const { return is_grounded; }
  void setGrounded(bool grounded) { is_grounded = grounded; }

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
  virtual void takeDamage(float health_damage, float posture_damage, Character* attacker = nullptr) = 0;

protected:
  unsigned int id;
  Faction faction;
  Vector3 position;
  Vector3 rotation;
  Stats stats;

  float vertical_velocity = 0.0f;
  Vector3 horizontal_velocity = {0.0f, 0.0f, 0.0f}; // x,z used; y unused
  bool is_grounded = true;

private:
  inline static unsigned int next_id = 1;
};