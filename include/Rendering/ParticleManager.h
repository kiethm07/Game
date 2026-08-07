#pragma once
#include <raylib.h>
#include <vector>

enum class ParticleType {
    SPARK,
    SMOKE
};

struct Particle {
    ParticleType type;
    Vector3 position;
    Vector3 velocity;
    Color color;
    float life;
    float max_life;
    float size;
};

class ParticleManager {
public:
    ParticleManager() = default;
    ~ParticleManager() = default;

    // Emits bright sparks that shoot out quickly and fade
    void emitSparks(Vector3 position, int count);
    
    // Emits slow-moving, expanding smoke spheres
    void emitVisualSmoke(Vector3 position, float radius, float duration);
    
    void update(float dt);
    
    // Must be called inside a BeginMode3D block
    void draw() const;

private:
    std::vector<Particle> particles;
};
