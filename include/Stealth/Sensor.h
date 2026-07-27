#pragma once

#include <raylib.h>
#include <raymath.h>

class Sensor {
public:
    virtual ~Sensor() = default;
    
    // Returns true if the target is detected by this sensor.
    virtual bool checkDetection(const Vector3& observer_pos, const Vector3& target_pos) const = 0;
};

class RadiusSensor : public Sensor {
public:
    float radius;
    
    RadiusSensor(float detection_radius) : radius(detection_radius) {}

    bool checkDetection(const Vector3& observer_pos, const Vector3& target_pos) const override {
        float dist_sq = Vector3DistanceSqr(observer_pos, target_pos);
        return dist_sq <= (radius * radius);
    }
};
