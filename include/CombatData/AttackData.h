#pragma once
#include <CombatData/ArmorType.h>
#include <vector>
#include <raylib.h>
#include <raymath.h>

enum class HitBoxShapeType { Sphere, Capsule };

struct HitBoxDefinition {
    HitBoxShapeType type;

    // For Sphere
    float forward_offset;
    float vertical_offset;
    float radius;

    // For Capsule
    Vector3 start_offset;
    Vector3 end_offset;
    float capsule_radius;

    float health_damage;
    float posture_damage;

    // Helper constructors
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
    AttackData(float startup_duration, float active_duration, float recovery_duration):
        startup_duration(startup_duration),
        active_duration(active_duration),
        recovery_duration(recovery_duration)
    {}
    AttackData(float startup_duration, float active_duration, float recovery_duration,
               const char* clip_name, bool use_root_motion = true):
        startup_duration(startup_duration),
        active_duration(active_duration),
        recovery_duration(recovery_duration),
        clip_name(clip_name),
        use_root_motion(use_root_motion)
    {}
    ~AttackData() = default;

    float getStartupDuration() const { return startup_duration; }
    float getActiveDuration() const { return active_duration; }
    float getRecoveryDuration() const { return recovery_duration; }
    ArmorType getArmorType() const { return armor_type; }
    float getStaggerDamage() const { return stagger_damage; }

    /// Name of the clip this attack plays, or nullptr when the asset has no
    /// dedicated animation yet. Looked up by name because clip indices shift
    /// with the export toolchain (see AssetManager::findAnimation).
    const char* getClipName() const { return clip_name; }

    /// Whether the attack's displacement comes from the clip's root motion.
    /// False pins the character in place for the duration, which is the right
    /// answer for an attack whose clip carries no authored travel.
    bool usesRootMotion() const { return use_root_motion && clip_name != nullptr; }

    const std::vector<HitBoxDefinition>& getHitBoxDefs() const { return hitbox_defs; }
    void addHitBoxDef(const HitBoxDefinition& def) { hitbox_defs.push_back(def); }

private:
    std::vector<HitBoxDefinition> hitbox_defs;

    float startup_duration   = 0.0f; //No specific data yet
    float active_duration    = 0.0f; //No specific data yet
    float recovery_duration  = 0.0f; //No specific data yet

    ArmorType armor_type = ArmorType::Interruptible;
    float stagger_damage = 0.0f;

    const char* clip_name = nullptr;
    bool use_root_motion = false;
};