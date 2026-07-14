#pragma once
#include <raylib.h>

class TimeManager {
    
public:
    TimeManager() : 
        delta_time(0.0f), total_time(0.0f), frame_count(0) 
    {}
    ~TimeManager() = default;

    void update();
    float getDeltaTime() const;    
    float getTotalTime() const;    
    unsigned int getFrameCount() const;

private:
    float delta_time;
    float total_time;
    unsigned int frame_count;
};