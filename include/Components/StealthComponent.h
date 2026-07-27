#pragma once

#include <Stealth/Sensor.h>
#include <vector>
#include <memory>
#include <string>

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

    bool isPlayerDetected() const {
        return is_player_detected;
    }

    void setPlayerDetected(bool detected) {
        is_player_detected = detected;
        if (detected) {
            debug_state = "DETECTED";
        } else {
            debug_state = "IDLE";
        }
    }

    const std::string& getDebugState() const {
        return debug_state;
    }

private:
    std::vector<std::shared_ptr<Sensor>> sensors;
    bool is_player_detected = false;
    std::string debug_state = "IDLE";
};
