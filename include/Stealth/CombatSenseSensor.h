#pragma once

#include <Stealth/Sensor.h>
#include <Entities/Enemy.h>
#include <raymath.h>
#include <cmath>

class CombatSenseSensor : public Sensor {
public:
    float radius;
    CombatSenseSensor(float detection_radius) 
        : radius(detection_radius) {}

    float getDetectionStrength(const Character* observer, const Character* target, const std::vector<PhysicsObstacle>& obstacles) const override {
        // Only active if the observer is an Enemy and is already in Combat (Aware)
        const Enemy* enemy = dynamic_cast<const Enemy*>(observer);
        if (!enemy || enemy->getStealthComponent().getStealthState() != StealthState::Aware) {
            return 0.0f;
        }

        Vector3 obs_pos = observer->getPosition();
        Vector3 tgt_pos = target->getPosition();
        
        float dist_sq = Vector3DistanceSqr(obs_pos, tgt_pos);
        if (dist_sq <= (radius * radius)) {
            // Return maximum strength to ensure they stay Aware as long as player is close
            return 5.0f;
        }
        return 0.0f;
    }

    void drawDebug(const Character* observer) const override {
        const Enemy* enemy = dynamic_cast<const Enemy*>(observer);
        if (enemy && enemy->getStealthComponent().getStealthState() == StealthState::Aware) {
            Vector3 pos = observer->getPosition();
            int segments = 16;
            float angle_step = 2.0f * PI / segments;
            Vector3 prev_pt = {pos.x + radius, pos.y + 0.1f, pos.z};
            for(int i = 1; i <= segments; ++i) {
                float angle = i * angle_step;
                Vector3 pt = {pos.x + std::cos(angle) * radius, pos.y + 0.1f, pos.z + std::sin(angle) * radius};
                DrawLine3D(prev_pt, pt, ORANGE);
                prev_pt = pt;
            }
        }
    }
};
