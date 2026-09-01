#pragma once

#include "raylib.h"
#include <Components/Faction.h>
#include <Components/HitBox.h>
#include <Components/HurtBox.h>
#include <Components/Stats.h>
#include <Rendering/AssetManager.h>
#include <Rendering/RenderData.h>
#include <Components/PhysicsObstacle.h>
#include <vector>

class NavMeshQuery;
struct SmokeCloud;
class SoundController;

enum class DamageResult {
    IGNORED,
    HIT,
    BLOCKED,
    PARRIED
};

class Character;

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
  
  /// Read-only access to the navigation query for AI pathfinding.
  const NavMeshQuery *nav_query = nullptr;
  
  /// Read-only access to obstacles for local steering avoidance.
  const std::vector<PhysicsObstacle> *obstacles = nullptr;
  
  /// Read-only access to current active smoke clouds.
  const std::vector<SmokeCloud> *smoke_clouds = nullptr;

  /// The character the player is currently locked onto, if any.
  Character *lockedTarget = nullptr;

  /// Read-only access to all active characters for separation steering.
  const std::vector<Character*> *activeCharacters = nullptr;

  /// Non-owning pointer to SoundController for triggering audio effects.
  SoundController *sound_controller = nullptr;
};

class Character {
public:
  Character(Faction faction);
  virtual ~Character() = default;

  virtual void update(const UpdateContext &ctx) = 0;
  virtual CharacterRenderData getRenderData() const = 0;
  virtual void draw() const;
  virtual void drawTrail() const {}

  Vector3 getPosition() const { return position; }

  void setPosition(const Vector3 &new_position) { position = new_position; }

  Vector3 getRotation() const { return rotation; }
  void setRotation(const Vector3 &new_rotation) { rotation = new_rotation; }

  const Stats &getStats() const { return stats; }
  Stats &getMutableStats() { return stats; }
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

  bool isGhost() const { return is_ghost; }
  void setGhost(bool ghost) { is_ghost = ghost; }

  virtual bool isCrouching() const { return false; }
  virtual bool isExecuting() const { return false; }
  virtual bool isBeingExecuted() const { return false; }

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
  virtual DamageResult takeDamage(float health_damage, float posture_damage, Character* attacker = nullptr) = 0;

  /// Called on the ATTACKER when the swing that just landed was deflected.
  ///
  /// Separate from takeDamage() because a deflect is a weapon clash, not a
  /// wound. Routed through the damage path it looked like one: the mini boss
  /// queued the flinch it plays for a blade in the ribs, the animation ladder
  /// held that back under the swing it was still committed to, and the flinch
  /// then surfaced once the attack ended -- so deflecting made the boss finish
  /// its swing and only afterwards double over at nothing.
  ///
  /// The posture cost is the same either way. What a deflect must NOT do is
  /// interrupt the attacker's own action: in a combo, being deflected on the
  /// first swing is not a reason for the second one to stop coming.
  virtual void onAttackDeflected(float posture_damage) {
    takeDamage(0.0f, posture_damage, nullptr);
  }

protected:
  unsigned int id;
  Faction faction;
  Vector3 position;
  Vector3 rotation;
  Stats stats;

  float vertical_velocity = 0.0f;
  Vector3 horizontal_velocity = {0.0f, 0.0f, 0.0f}; // x,z used; y unused
  bool is_grounded = true;
  bool is_ghost = false;

private:
  inline static unsigned int next_id = 1;
};