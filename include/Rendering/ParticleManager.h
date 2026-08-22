#pragma once
#include <raylib.h>
#include <vector>

enum class ParticleType {
    SPARK,
    SMOKE,
    BLOOD
};

struct Particle {
    ParticleType type;
    Vector3 position;
    Vector3 velocity;
    Color color;
    float life;
    float max_life;
    float size;

    /// The height this particle bounces off, in world Y.
    ///
    /// Per particle because there is no single floor to hardcode: this game's
    /// terrain runs from about -39 to +56, and it used to bounce everything off
    /// y = 0.05. Below that line -- which is most of phase1 and phase2, where
    /// enemies stand as low as -3.3 -- every particle was teleported up to
    /// y = 0.05 on its first frame and had its velocity killed by the bounce,
    /// so a hit downhill sprayed a flat, feeble sheet somewhere above the
    /// target's head. Emitters pass the ground under the thing that was hit.
    float floor_y;
};

class ParticleManager {
public:
    ParticleManager() = default;
    ~ParticleManager() = default;

    // Emits bright sparks that shoot out quickly and fade.
    // `floor_y` is the ground under the impact -- see Particle::floor_y.
    void emitSparks(Vector3 position, int count, float floor_y);

    // Emits blood splatter particles.
    // `floor_y` is the ground under the impact -- see Particle::floor_y.
    void emitBlood(Vector3 position, int count, float floor_y);
    
    // Emits slow-moving, expanding smoke spheres
    void emitVisualSmoke(Vector3 position, float radius, float duration);
    
    void update(float dt);
    
    // Must be called inside a BeginMode3D block
    void draw() const;

private:
    std::vector<Particle> particles;
};
