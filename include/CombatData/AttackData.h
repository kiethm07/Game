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

/// One hit window inside an attack: wind up, then a live hitbox.
///
/// An attack used to BE one of these -- every clip in the game held a single
/// swing, so the phase durations could live directly on AttackData. The mini
/// boss's combo clips hold two and three swings each, and they have to stay one
/// attack rather than become a chain of them: CombatComponent bumps its action
/// id per attack, and that id is exactly what the animator rewinds the clip on,
/// so a chain would restart the combo animation at frame 0 for every swing.
struct SwingWindow {
    /// Seconds from the end of the previous window -- or from the attack's
    /// start, for the first one -- until this hitbox goes live.
    float windup = 0.0f;

    /// Seconds the hitbox stays live.
    float active = 0.0f;

    /// Live only for this window. Kept per swing rather than per attack so a
    /// combo's swings can differ in shape as well as timing: the mini boss's
    /// three-hit finishes on an overhead chop, which is a narrow forward
    /// capsule where the two before it are wide lateral sweeps.
    std::vector<HitBoxDefinition> hitboxes;
};

class AttackData {
public:
    AttackData() = default;
    AttackData(float startup_duration, float active_duration, float recovery_duration):
        recovery_duration(recovery_duration)
    {
        swings.push_back({startup_duration, active_duration, {}});
    }
    AttackData(float startup_duration, float active_duration, float recovery_duration,
               const char* clip_name, bool use_root_motion = true):
        recovery_duration(recovery_duration),
        clip_name(clip_name),
        use_root_motion(use_root_motion)
    {
        swings.push_back({startup_duration, active_duration, {}});
    }
    ~AttackData() = default;

    /// Appends another hit window after the ones already authored. `windup` is
    /// measured from the end of the previous window, so the numbers read off a
    /// clip's timeline as the gap between one swing landing and the next.
    /// Subsequent addHitBoxDef() calls attach to this new window.
    void addSwing(float windup, float active) {
        swings.push_back({windup, active, {}});
    }

    /// How many hit windows this attack runs through before recovering. One for
    /// every attack that is a single swing.
    int getSwingCount() const { return static_cast<int>(swings.size()); }

    float getWindupDuration(int swing) const { return at(swing).windup; }
    float getActiveDuration(int swing) const { return at(swing).active; }
    float getRecoveryDuration() const { return recovery_duration; }

    /// Everything the attack's phases plus its recovery add up to. The clip is
    /// the reference for this: it should sit just under the clip's playable
    /// length so the state machine ends when the animation does.
    float getTotalDuration() const {
        float total = recovery_duration;
        for (const SwingWindow& swing : swings) {
            total += swing.windup + swing.active;
        }
        return total;
    }

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

    /// The hitboxes live during one particular swing. Callers pass the index
    /// CombatComponent is currently timing against, which for a single-swing
    /// attack is always 0.
    const std::vector<HitBoxDefinition>& getHitBoxDefs(int swing) const {
        return at(swing).hitboxes;
    }

    /// Adds a hitbox to the LAST swing authored, so a combo reads top to bottom
    /// in the registry: addSwing, its hitboxes, addSwing, its hitboxes.
    void addHitBoxDef(const HitBoxDefinition& def) {
        // Only a default-constructed AttackData has no window to hang this on;
        // every other constructor opens one. Giving it a zero-length window
        // rather than writing past the end keeps the call safe on an attack
        // whose durations were never filled in.
        if (swings.empty()) swings.push_back({});
        swings.back().hitboxes.push_back(def);
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
    /// At least one after any constructor but the default one. Clamped access
    /// rather than a bare index: an out-of-range swing is a bug in the caller,
    /// but the answer to it is a hitbox that does nothing, not a read past the
    /// end during the damage pass.
    const SwingWindow& at(int swing) const {
        static const SwingWindow kNone{};
        if (swings.empty()) return kNone;
        if (swing < 0) swing = 0;
        if (swing >= static_cast<int>(swings.size())) swing = static_cast<int>(swings.size()) - 1;
        return swings[static_cast<size_t>(swing)];
    }

    std::vector<SwingWindow> swings;

    float recovery_duration  = 0.0f; //No specific data yet

    ArmorType armor_type = ArmorType::Interruptible;
    float stagger_damage = 0.0f;

    const char* clip_name = nullptr;
    bool use_root_motion = false;

    bool has_trail = false;
    float trail_duration = 0.20f;
    Vector3 blade_vector = {0.0f, 1.2f, 0.0f};
    Vector3 hilt_vector = {0.0f, 0.1f, 0.0f};
};
