#pragma once

#include <raylib.h>
#include <raymath.h>
#include <Entities/Character.h>
#include <Components/PhysicsObstacle.h>
#include <Physics/CollisionMesh.h>
#include <cmath>
#include <GameManager/SmokeCloud.h>

/// Is the straight line from `a` to `b` interrupted by the world?
///
/// Checks the BOX_/RAMP_ proxies and, where the level has one, its collision
/// mesh. The mesh is not optional in practice any more: once the castle's
/// walls and gatehouse stopped being box proxies, a proxy-only test could see
/// straight through them, and enemies would spot the player through a wall.
inline bool segmentBlocked(Vector3 a, Vector3 b,
                           const std::vector<PhysicsObstacle>& obstacles,
                           const CollisionMesh* mesh) {
    const float span = Vector3Distance(a, b);
    if (span < 1e-4f) return false;
    const Vector3 direction = Vector3Scale(Vector3Subtract(b, a), 1.0f / span);

    if (mesh != nullptr && !mesh->isEmpty()) {
        MeshHit hit;
        if (mesh->raycast(a, direction, span, hit)) return true;
    }

    for (const auto& obs : obstacles) {
        Vector3 local_a = Vector3Transform(a, obs.getWorldToLocal());
        Vector3 local_b = Vector3Transform(b, obs.getWorldToLocal());
        Ray ray;
        ray.position = local_a;
        ray.direction = Vector3Normalize(Vector3Subtract(local_b, local_a));
        RayCollision col = GetRayCollisionBox(ray, obs.getLocalBox());
        if (col.hit && col.distance < Vector3Distance(local_a, local_b)) {
            return true;
        }
    }
    return false;
}

class Sensor {
public:
    virtual ~Sensor() = default;
    
    // Returns 0.0f if not detected. Returns > 0.0f (usually 0.0f to 1.0f) representing the strength of detection.
    virtual float getDetectionStrength(const Character* observer, const Character* target, const std::vector<PhysicsObstacle>& obstacles, const CollisionMesh* mesh, const std::vector<SmokeCloud>& smoke_clouds) const = 0;
    
    // Renders the sensor boundaries in 3D for debugging
    virtual void drawDebug(const Character* observer) const {}
};

class RadiusSensor : public Sensor {
public:
    float radius;
    
    RadiusSensor(float detection_radius) : radius(detection_radius) {}

    float getDetectionStrength(const Character* observer, const Character* target, const std::vector<PhysicsObstacle>& obstacles, const CollisionMesh* mesh, const std::vector<SmokeCloud>& smoke_clouds) const override {
        float dist_sq = Vector3DistanceSqr(observer->getPosition(), target->getPosition());
        if (dist_sq <= (radius * radius)) {
            return 1.0f; // Could scale by distance later
        }
        return 0.0f;
    }
};

class VisionSensor : public Sensor {
public:
    float radius;
    float cone_angle_degrees; // e.g., 90.0f for a 90-degree cone

    VisionSensor(float detection_radius, float angle_degrees = 90.0f) 
        : radius(detection_radius), cone_angle_degrees(angle_degrees) {}

    float getDetectionStrength(const Character* observer, const Character* target, const std::vector<PhysicsObstacle>& obstacles, const CollisionMesh* mesh, const std::vector<SmokeCloud>& smoke_clouds) const override {
        Vector3 obs_pos = observer->getPosition();
        Vector3 tgt_pos = target->getPosition();
        
        float dist_sq = Vector3DistanceSqr(obs_pos, tgt_pos);
        if (dist_sq > (radius * radius)) {
            return 0.0f;
        }

        // --- Line of Sight Raycast preparation ---
        // Calculate true 3D angle from head/eyes to head/eyes
        Vector3 obs_head = { obs_pos.x, obs_pos.y + observer->getColliderHeight() * 0.8f, obs_pos.z };
        Vector3 tgt_head = { tgt_pos.x, tgt_pos.y + target->getColliderHeight() * 0.8f, tgt_pos.z };
        
        Vector3 to_target = Vector3Subtract(tgt_head, obs_head);
        if (Vector3LengthSqr(to_target) < 0.001f) return 1.0f; // Standing exactly on same spot
        
        to_target = Vector3Normalize(to_target);
        
        // Observer's forward vector based on rotation.y (assuming looking horizontally)
        // If enemies can pitch their head, we'd include pitch here, but for now yaw is enough.
        float yaw_rad = observer->getRotation().y * DEG2RAD;
        Vector3 forward = {std::sin(yaw_rad), 0.0f, std::cos(yaw_rad)};
        
        // True 3D dot product calculates the spherical cone angle
        float dot = Vector3DotProduct(forward, to_target);
        float angle_to_target = std::acos(dot) * RAD2DEG;
        
        if (angle_to_target > (cone_angle_degrees / 2.0f)) {
            return 0.0f;
        }

        // --- Line of Sight Raycast ---
        // Cast from observer head to target head AND target feet
        Vector3 tgt_foot = tgt_pos;
        // Head and feet are tested separately so a low wall that hides the body
        // but not the head still leaks sight, which is what makes crouching
        // behind cover read correctly.
        const bool head_blocked = segmentBlocked(obs_head, tgt_head, obstacles, mesh);
        const bool feet_blocked = segmentBlocked(obs_head, tgt_foot, obstacles, mesh);
        if (head_blocked && feet_blocked) {
            return 0.0f;
        }
        
        // --- Smoke Cloud Intersection ---
        float distance_to_target = std::sqrt(dist_sq);
        for (const auto& smoke : smoke_clouds) {
            if (smoke.owner == observer) continue; // Immune to own smoke

            Vector3 L = Vector3Subtract(smoke.position, obs_head);
            float tca = Vector3DotProduct(L, to_target);
            if (tca < 0.0f) continue;

            float d2 = Vector3DotProduct(L, L) - tca * tca;
            float r2 = smoke.radius * smoke.radius;
            if (d2 > r2) continue;

            float thc = std::sqrt(r2 - d2);
            float t0 = tca - thc; // first hit distance

            if (t0 < distance_to_target && t0 > 0.0f) {
                return 0.0f; // Vision blocked by smoke!
            }
        }
        
        float dist = distance_to_target;
        float normalized_dist = dist / radius;
        
        // Logarithmic scale: drops rapidly at first, then tails off.
        // Strength scales from 5.75x (point blank) down to 1.70x (farthest).
        //
        // The far end used to bottom out at 0.25x, which combined with the
        // angle factor's 0.5x gave the cone a 60x spread between its best and
        // worst corner. An enemy needs 200 awareness points at build_rate 50 to
        // reach DETECTED, so that corner cost 28 seconds -- the player could
        // stand upright, unobstructed, well inside a 20 m vision radius and
        // watch the bar crawl. The radius is the design statement about how far
        // this enemy can see; anywhere inside it should resolve in seconds, and
        // the falloff should say "sooner when closer", not "never when far".
        // Point blank is untouched; the floor is lifted ~7x, leaving a 6x
        // spread across the whole cone.
        float strength = 5.75f - 4.05f * std::log10(1.0f + normalized_dist * 9.0f);
        
        // Scale by angle logarithmically: 1.4x directly in front, dropping to
        // 0.8x at the edge -- flattened alongside the distance curve above, and
        // for the same reason. Peripheral vision should be a touch slower than
        // a dead-on stare, not effectively blind.
        float max_angle = cone_angle_degrees / 2.0f;
        float normalized_angle = angle_to_target / max_angle;
        float angle_factor = 1.4f - 0.6f * std::log10(1.0f + 9.0f * normalized_angle);
        strength *= angle_factor;

        if (target->isCrouching()) {
            strength *= 0.4f; // Harder to see when crouching
        }

        return strength;
    }

    void drawDebug(const Character* observer) const override {
        Vector3 pos = observer->getPosition();
        float yaw_rad = observer->getRotation().y * DEG2RAD;
        float half_cone = (cone_angle_degrees / 2.0f) * DEG2RAD;

        Vector3 left_dir = {std::sin(yaw_rad - half_cone), 0.0f, std::cos(yaw_rad - half_cone)};
        Vector3 right_dir = {std::sin(yaw_rad + half_cone), 0.0f, std::cos(yaw_rad + half_cone)};

        Vector3 left_pt = {pos.x + left_dir.x * radius, pos.y + 0.1f, pos.z + left_dir.z * radius};
        Vector3 right_pt = {pos.x + right_dir.x * radius, pos.y + 0.1f, pos.z + right_dir.z * radius};
        Vector3 center_pt = {pos.x, pos.y + 0.1f, pos.z};

        DrawLine3D(center_pt, left_pt, RED);
        DrawLine3D(center_pt, right_pt, RED);
        
        int segments = 10;
        float angle_step = cone_angle_degrees * DEG2RAD / segments;
        Vector3 prev_pt = left_pt;
        for(int i = 1; i <= segments; ++i) {
            float angle = (yaw_rad - half_cone) + i * angle_step;
            Vector3 dir = {std::sin(angle), 0.0f, std::cos(angle)};
            Vector3 pt = {pos.x + dir.x * radius, pos.y + 0.1f, pos.z + dir.z * radius};
            DrawLine3D(prev_pt, pt, RED);
            prev_pt = pt;
        }
    }
};

class SoundSensor : public Sensor {
public:
    float radius;
    
    SoundSensor(float detection_radius) : radius(detection_radius) {}

    float getDetectionStrength(const Character* observer, const Character* target, const std::vector<PhysicsObstacle>& obstacles, const CollisionMesh* mesh, const std::vector<SmokeCloud>& smoke_clouds) const override {
        float dist_sq = Vector3DistanceSqr(observer->getPosition(), target->getPosition());
        if (dist_sq > (radius * radius)) {
            return 0.0f;
        }

        Vector3 vel = target->getHorizontalVelocity();
        float speed = Vector3Length(vel);

        // If not moving, no sound is made.
        if (speed < 0.1f) {
            return 0.0f;
        }

        // Speed factor: 1.0f at normal movement speed (~4.0 units/sec)
        float speed_factor = std::fmin(speed / 4.0f, 1.0f);
        
        // Distance factor: 1.0f at exactly observer pos, 0.0f at max radius
        float dist = std::sqrt(dist_sq);
        float normalized_dist = dist / radius;
        
        // Logarithmic scale for distance (5.0x at point blank down to 0.25x at max radius)
        float dist_strength = 5.0f - 4.75f * std::log10(1.0f + normalized_dist * 9.0f);
        
        float strength = speed_factor * dist_strength;
        if (target->isCrouching()) {
            strength *= 0.1f; // Sneaking is very quiet
        }
        
        return strength;
    }

    void drawDebug(const Character* observer) const override {
        Vector3 pos = observer->getPosition();
        int segments = 20;
        float angle_step = 2.0f * PI / segments;
        Vector3 prev_pt = {pos.x + radius, pos.y + 0.1f, pos.z};
        for(int i = 1; i <= segments; ++i) {
            float angle = i * angle_step;
            Vector3 pt = {pos.x + std::cos(angle) * radius, pos.y + 0.1f, pos.z + std::sin(angle) * radius};
            DrawLine3D(prev_pt, pt, BLUE);
            prev_pt = pt;
        }
    }
};

class ProximitySensor : public Sensor {
public:
    float radius;
    
    ProximitySensor(float detection_radius) : radius(detection_radius) {}

    float getDetectionStrength(const Character* observer, const Character* target, const std::vector<PhysicsObstacle>& obstacles, const CollisionMesh* mesh, const std::vector<SmokeCloud>& smoke_clouds) const override {
        float dist_sq = Vector3DistanceSqr(observer->getPosition(), target->getPosition());
        if (dist_sq <= (radius * radius)) {
            // Check if blocked by walls (so you can't be instantly detected through a thin wall if you bump it)
            Vector3 obs_head = { observer->getPosition().x, observer->getPosition().y + observer->getColliderHeight() * 0.8f, observer->getPosition().z };
            Vector3 tgt_head = { target->getPosition().x, target->getPosition().y + target->getColliderHeight() * 0.8f, target->getPosition().z };
            
            if (segmentBlocked(obs_head, tgt_head, obstacles, mesh)) {
                return 0.0f; // Blocked by wall
            }
            return 9999.0f; // Instant detect
        }
        return 0.0f;
    }

    void drawDebug(const Character* observer) const override {
        Vector3 pos = observer->getPosition();
        int segments = 10;
        float angle_step = 2.0f * PI / segments;
        Vector3 prev_pt = {pos.x + radius, pos.y + 0.1f, pos.z};
        for(int i = 1; i <= segments; ++i) {
            float angle = i * angle_step;
            Vector3 pt = {pos.x + std::cos(angle) * radius, pos.y + 0.1f, pos.z + std::sin(angle) * radius};
            DrawLine3D(prev_pt, pt, MAGENTA);
            prev_pt = pt;
        }
    }
};
