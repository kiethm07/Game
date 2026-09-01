#pragma once
#include <CombatData/ArmorType.h>
#include <vector>
#include <raylib.h>
#include <raymath.h>

enum class HitBoxShapeType { Sphere, Capsule };

class HitBoxDefinition {
public:
    HitBoxShapeType type;

    // For Sphere
    float forward_offset = 0.0f;
    float vertical_offset = 0.0f;
    float radius = 0.0f;

    // For Capsule
    Vector3 start_offset = {0.0f, 0.0f, 0.0f};
    Vector3 end_offset = {0.0f, 0.0f, 0.0f};
    float capsule_radius = 0.0f;

    float health_damage = 0.0f;
    float posture_damage = 0.0f;

    static HitBoxDefinition createSphere(float fwd, float vert, float r, float hd, float pd) {
        HitBoxDefinition def;
        def.type = HitBoxShapeType::Sphere;
        def.forward_offset = fwd;
        def.vertical_offset = vert;
        def.radius = r;
        def.health_damage = hd;
        def.posture_damage = pd;
        return def;
    }

    static HitBoxDefinition createCapsule(Vector3 start, Vector3 end, float r, float hd, float pd) {
        HitBoxDefinition def;
        def.type = HitBoxShapeType::Capsule;
        def.start_offset = start;
        def.end_offset = end;
        def.capsule_radius = r;
        def.health_damage = hd;
        def.posture_damage = pd;
        return def;
    }
};

class AttackData {
public:
    AttackData() = default;
    AttackData(float startup_duration, float active_duration, float recovery_duration)
        : startup_duration(startup_duration),
          active_duration(active_duration),
          recovery_duration(recovery_duration) {}
    AttackData(float startup_duration, float active_duration, float recovery_duration,
               const char* clip_name, bool use_root_motion = true)
        : startup_duration(startup_duration),
          active_duration(active_duration),
          recovery_duration(recovery_duration),
          clip_name(clip_name),
          start_time(0.0f),
          use_root_motion(use_root_motion) {}
    AttackData(float startup_duration, float active_duration, float recovery_duration,
               const char* clip_name, float start_time, bool use_root_motion = true)
        : startup_duration(startup_duration),
          active_duration(active_duration),
          recovery_duration(recovery_duration),
          clip_name(clip_name),
          start_time(start_time),
          use_root_motion(use_root_motion) {}
    ~AttackData() = default;

    float getStartupDuration() const { return startup_duration; }
    float getWindupDuration(int = 0) const { return startup_duration; }
    float getActiveDuration(int = 0) const { return active_duration; }
    float getRecoveryDuration() const { return recovery_duration; }
    float getTotalDuration() const { return startup_duration + active_duration + recovery_duration; }

    float getStartTime() const { return start_time; }
    const char* getClipName() const { return clip_name; }
    bool usesRootMotion() const { return use_root_motion && clip_name != nullptr; }

    ArmorType getArmorType() const { return armor_type; }
    float getStaggerDamage() const { return stagger_damage; }

    const std::vector<HitBoxDefinition>& getHitBoxDefs() const { return hitboxes; }
    const std::vector<HitBoxDefinition>& getHitBoxDefs(int) const { return hitboxes; }

    void addHitBoxDef(const HitBoxDefinition& def) {
        hitboxes.push_back(def);
    }

    void addSwing(float = 0.0f, float = 0.0f) {}

    float getAdvanceSpeed() const { return advance_speed; }
    float getAdvanceStopDistance() const { return advance_stop; }
    float getAdvanceTurnRate() const { return advance_turn; }

    void setAdvance(float speed, float stop_distance, float turn_rate) {
        advance_speed = speed;
        advance_stop = stop_distance;
        advance_turn = turn_rate;
    }

    bool hasTrail() const { return has_trail; }
    float getTrailDuration() const { return trail_duration; }
    Vector3 getBladeVector() const { return blade_vector; }
    Vector3 getHiltVector() const { return hilt_vector; }

    void setTrail(bool enable, float duration = 0.20f, Vector3 blade = {0.0f, 1.2f, 0.0f}, Vector3 hilt = {0.0f, 0.1f, 0.0f}) {
        has_trail = enable;
        trail_duration = duration;
        blade_vector = blade;
        hilt_vector = hilt;
    }

private:
    float startup_duration = 0.0f;
    float active_duration = 0.0f;
    float recovery_duration = 0.0f;

    std::vector<HitBoxDefinition> hitboxes;

    const char* clip_name = nullptr;
    float start_time = 0.0f;
    bool use_root_motion = false;

    ArmorType armor_type = ArmorType::Interruptible;
    float stagger_damage = 0.0f;

    float advance_speed = 0.0f;
    float advance_stop = 0.0f;
    float advance_turn = 0.0f;

    bool has_trail = false;
    float trail_duration = 0.20f;
    Vector3 blade_vector = {0.0f, 1.2f, 0.0f};
    Vector3 hilt_vector = {0.0f, 0.1f, 0.0f};
};
