#pragma once

#include <Stealth/Sensor.h>
#include <vector>
#include <memory>
#include <string>

enum class StealthState {
    Unaware,
    Suspicious,
    Aware
};

class StealthComponent {
public:
    StealthComponent() = default;
    ~StealthComponent() = default;

    void addSensor(std::shared_ptr<Sensor> sensor) {
        sensors.push_back(std::move(sensor));
    }

    const std::vector<std::shared_ptr<Sensor>>& getSensors() const {
        return sensors;
    }

    void updateAwareness(float detection_strength, float dt) {
        if (detection_strength > 0.0f) {
            awareness_level += detection_strength * build_rate * dt;
            if (awareness_level > 200.0f) awareness_level = 200.0f;
        } else {
            awareness_level -= decay_rate * dt;
            if (awareness_level < 0.0f) awareness_level = 0.0f;
        }

        if (awareness_level < 100.0f) {
            current_state = StealthState::Unaware;
            debug_state = "UNAWARE";
        } else if (awareness_level < 200.0f) {
            current_state = StealthState::Suspicious;
            debug_state = "SUSPICIOUS";
        } else {
            current_state = StealthState::Aware;
            debug_state = "DETECTED";
        }
    }

    bool isPlayerDetected() const {
        return current_state == StealthState::Aware;
    }

    StealthState getStealthState() const { return current_state; }
    float getAwarenessLevel() const { return awareness_level; }

    const std::string& getDebugState() const {
        return debug_state;
    }
    
    Vector3 getLastKnownPlayerPos() const { return last_known_player_pos; }
    void setLastKnownPlayerPos(const Vector3& pos) { last_known_player_pos = pos; }

private:
    std::vector<std::shared_ptr<Sensor>> sensors;
    StealthState current_state = StealthState::Unaware;
    float awareness_level = 0.0f; // 0-99.9 = Unaware, 100-199.9 = Sus, 200 = Aware
    
    Vector3 last_known_player_pos = {0.0f, 0.0f, 0.0f};
    
    float build_rate = 50.0f; // Points per second
    float decay_rate = 25.0f; // Points per second
    std::string debug_state = "UNAWARE";
};
