#include <Core/TimeManager.h>

void TimeManager::update() {
    delta_time = GetFrameTime();
    total_time = static_cast<float>(GetTime());
    frame_count++;
}

float TimeManager::getDeltaTime() const {
    return delta_time;
}

float TimeManager::getTotalTime() const {
    return total_time;
}

unsigned int TimeManager::getFrameCount() const {
    return frame_count;
}