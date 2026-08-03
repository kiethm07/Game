#pragma once

#include <raylib.h>
#include <raymath.h>
#include <Entities/Character.h>
#include <Components/PhysicsObstacle.h>
#include <cmath>
#include <vector>

class Sensor {
public:
    virtual ~Sensor() = default;
    
    // Returns 0.0f if not detected. Returns > 0.0f (usually 0.0f to 1.0f) representing the strength of detection.
    virtual float getDetectionStrength(const Character* observer, const Character* target, const std::vector<PhysicsObstacle>& obstacles) const = 0;
    
    // Renders the sensor boundaries in 3D for debugging
    virtual void drawDebug(const Character* observer) const {}
};

class RadiusSensor : public Sensor {
public:
    float radius;
    
    RadiusSensor(float detection_radius) : radius(detection_radius) {}

    float getDetectionStrength(const Character* observer, const Character* target, const std::vector<PhysicsObstacle>& obstacles) const override {
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

    float getDetectionStrength(const Character* observer, const Character* target, const std::vector<PhysicsObstacle>& obstacles) const override {
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
        bool head_blocked = false;
        bool feet_blocked = false;
        
        for (const auto& obs : obstacles) {
            Vector3 local_obs = Vector3Transform(obs_head, obs.getWorldToLocal());
            
            // Check Head Ray
            if (!head_blocked) {
                Vector3 local_tgt_head = Vector3Transform(tgt_head, obs.getWorldToLocal());
                Ray ray_head;
                ray_head.position = local_obs;
                ray_head.direction = Vector3Normalize(Vector3Subtract(local_tgt_head, local_obs));
                RayCollision col_head = GetRayCollisionBox(ray_head, obs.getLocalBox());
                if (col_head.hit && col_head.distance < Vector3Distance(local_obs, local_tgt_head)) {
                    head_blocked = true;
                }
            }

            // Check Feet Ray
            if (!feet_blocked) {
                Vector3 local_tgt_foot = Vector3Transform(tgt_foot, obs.getWorldToLocal());
                Ray ray_foot;
                ray_foot.position = local_obs;
                ray_foot.direction = Vector3Normalize(Vector3Subtract(local_tgt_foot, local_obs));
                RayCollision col_foot = GetRayCollisionBox(ray_foot, obs.getLocalBox());
                if (col_foot.hit && col_foot.distance < Vector3Distance(local_obs, local_tgt_foot)) {
                    feet_blocked = true;
                }
            }
            
            if (head_blocked && feet_blocked) {
                return 0.0f;
            }
        }
        
        float dist = std::sqrt(dist_sq);
        float normalized_dist = dist / radius;
        
        // Logarithmic scale: Drops rapidly at first, then tails off.
        // Strength scales from 5.0x (point blank) down to 0.25x (farthest).
        float strength = 5.0f - 4.75f * std::log10(1.0f + normalized_dist * 9.0f);
        
        // Scale by angle logarithmically: 1.5x directly in front, dropping to 0.5x at the edge
        float max_angle = cone_angle_degrees / 2.0f;
        float normalized_angle = angle_to_target / max_angle;
        float angle_factor = 1.5f - std::log10(1.0f + 9.0f * normalized_angle);
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

    float getDetectionStrength(const Character* observer, const Character* target, const std::vector<PhysicsObstacle>& obstacles) const override {
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

    float getDetectionStrength(const Character* observer, const Character* target, const std::vector<PhysicsObstacle>& obstacles) const override {
        float dist_sq = Vector3DistanceSqr(observer->getPosition(), target->getPosition());
        if (dist_sq <= (radius * radius)) {
            // Check if blocked by walls (so you can't be instantly detected through a thin wall if you bump it)
            Vector3 obs_head = { observer->getPosition().x, observer->getPosition().y + observer->getColliderHeight() * 0.8f, observer->getPosition().z };
            Vector3 tgt_head = { target->getPosition().x, target->getPosition().y + target->getColliderHeight() * 0.8f, target->getPosition().z };
            
            for (const auto& obs : obstacles) {
                Vector3 local_obs = Vector3Transform(obs_head, obs.getWorldToLocal());
                Vector3 local_tgt = Vector3Transform(tgt_head, obs.getWorldToLocal());
                
                Ray local_ray;
                local_ray.position = local_obs;
                local_ray.direction = Vector3Normalize(Vector3Subtract(local_tgt, local_obs));
                
                RayCollision collision = GetRayCollisionBox(local_ray, obs.getLocalBox());
                if (collision.hit) {
                    float dist_to_tgt_local = Vector3Distance(local_obs, local_tgt);
                    if (collision.distance < dist_to_tgt_local) {
                        return 0.0f; // Blocked by wall
                    }
                }
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
