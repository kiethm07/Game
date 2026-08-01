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

        Vector3 to_target = Vector3Subtract(tgt_pos, obs_pos);
        to_target.y = 0.0f; // Ignore height for the cone calculation
        if (Vector3LengthSqr(to_target) < 0.001f) return 1.0f; // Standing exactly on same spot
        
        to_target = Vector3Normalize(to_target);
        
        // Observer's forward vector based on rotation.y
        float yaw_rad = observer->getRotation().y * DEG2RAD;
        Vector3 forward = {std::sin(yaw_rad), 0.0f, std::cos(yaw_rad)};
        
        float dot = Vector3DotProduct(forward, to_target);
        float angle_to_target = std::acos(dot) * RAD2DEG;
        
        if (angle_to_target > (cone_angle_degrees / 2.0f)) {
            return 0.0f;
        }

        // --- Line of Sight Raycast ---
        // Cast from head/eyes to head/eyes
        Vector3 obs_head = { obs_pos.x, obs_pos.y + observer->getColliderHeight() * 0.8f, obs_pos.z };
        Vector3 tgt_head = { tgt_pos.x, tgt_pos.y + target->getColliderHeight() * 0.8f, tgt_pos.z };
        
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
                    // Blocked by this obstacle
                    return 0.0f;
                }
            }
        }
        
        float dist = std::sqrt(dist_sq);
        float normalized_dist = dist / radius;
        
        // Logarithmic scale: Drops rapidly at first, then tails off.
        // log10(1 + 9x) goes from 0 (at x=0) to 1 (at x=1).
        // Strength scales from 5.0x (point blank) down to 0.25x (farthest).
        float strength = 5.0f - 4.75f * std::log10(1.0f + normalized_dist * 9.0f);
        
        return strength;
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
        
        return speed_factor * dist_strength;
    }
};
