#pragma once

class AttackData {
public:
    AttackData() = default;
    AttackData(float startup_duration, float active_duration, float recovery_duration):
        startup_duration(startup_duration),
        active_duration(active_duration),
        recovery_duration(recovery_duration)
    {}
    ~AttackData() = default;

    float getStartupDuration() const { return startup_duration; }
    float getActiveDuration() const { return active_duration; }
    float getRecoveryDuration() const { return recovery_duration; }
private:
    float startup_duration   = 0.0f; //No specific data yet
    float active_duration    = 0.0f; //No specific data yet
    float recovery_duration  = 0.0f; //No specific data yet
};