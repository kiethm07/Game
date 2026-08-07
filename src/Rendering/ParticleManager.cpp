#include <Rendering/ParticleManager.h>
#include <raymath.h>
#include <cstdlib>
#include <cmath>

void ParticleManager::emitSparks(Vector3 position, int count) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.type = ParticleType::SPARK;
        p.position = position;
        
        // Spherical burst favoring upwards
        float u = ((float)rand() / RAND_MAX);
        float v = ((float)rand() / RAND_MAX);
        float theta = u * 2.0f * PI;
        float phi = acosf(2.0f * v - 1.0f);
        
        float dirX = sinf(phi) * cosf(theta);
        float dirY = std::abs(sinf(phi) * sinf(theta)) + 0.2f; // Always upward bias
        float dirZ = cosf(phi);
        
        Vector3 out_dir = Vector3Normalize({dirX, dirY, dirZ});
        
        // High explosive speed
        float speed = ((float)rand() / RAND_MAX) * 20.0f + 10.0f;
        p.velocity = Vector3Scale(out_dir, speed);
        
        // Sparks are usually intense yellow/white hot
        int colorType = rand() % 3;
        if (colorType == 0) p.color = { 255, 255, 150, 255 }; // Bright pale yellow
        else if (colorType == 1) p.color = { 255, 200, 0, 255 }; // Gold/Orange
        else p.color = WHITE;
        
        p.life = ((float)rand() / RAND_MAX) * 0.4f + 0.3f; // 0.3s - 0.7s
        p.max_life = p.life;
        p.size = ((float)rand() / RAND_MAX) * 0.03f + 0.015f; // Radius of the streak
        
        particles.push_back(p);
    }
}

void ParticleManager::emitBlood(Vector3 position, int count) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.type = ParticleType::BLOOD;
        p.position = position;
        
        // Clustered directional splash (narrower cone)
        float u = ((float)rand() / RAND_MAX);
        float theta = u * 2.0f * PI;
        float phi = ((float)rand() / RAND_MAX) * (PI / 3.0f); // 60 degree cone upwards
        
        float dirX = sinf(phi) * cosf(theta);
        float dirY = cosf(phi); // Y is up
        float dirZ = sinf(phi) * sinf(theta);
        
        Vector3 out_dir = Vector3Normalize({dirX, dirY, dirZ});
        
        float speed = ((float)rand() / RAND_MAX) * 12.0f + 3.0f; // clustered speeds
        p.velocity = Vector3Scale(out_dir, speed);
        
        int colorType = rand() % 3;
        if (colorType == 0) p.color = { 180, 0, 0, 255 }; 
        else if (colorType == 1) p.color = { 130, 0, 0, 255 }; 
        else p.color = { 220, 20, 20, 255 };
        
        p.life = ((float)rand() / RAND_MAX) * 0.5f + 0.3f; 
        p.max_life = p.life;
        p.size = ((float)rand() / RAND_MAX) * 0.06f + 0.03f; // slightly larger for clustered feel
        
        particles.push_back(p);
    }
}

void ParticleManager::emitVisualSmoke(Vector3 position, float radius, float duration) {
    // OPTIMIZATION: Reduce particle count drastically. 
    int count = (int)(radius * 6.0f); 
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.type = ParticleType::SMOKE;
        
        // Spherical random distribution within the radius
        float u = ((float)rand() / RAND_MAX);
        float v = ((float)rand() / RAND_MAX);
        float theta = u * 2.0f * PI;
        float phi = acosf(2.0f * v - 1.0f);
        float r = cbrtf((float)rand() / RAND_MAX) * radius * 0.8f; 
        
        float offsetX = r * sinf(phi) * cosf(theta);
        float offsetY = r * sinf(phi) * sinf(theta);
        float offsetZ = r * cosf(phi);
        
        p.position = {position.x + offsetX, position.y + std::abs(offsetY), position.z + offsetZ};
        
        // No upward fly, just very slight drift
        float driftX = ((float)rand() / RAND_MAX) * 0.2f - 0.1f;
        float driftY = ((float)rand() / RAND_MAX) * 0.1f;
        float driftZ = ((float)rand() / RAND_MAX) * 0.2f - 0.1f;
        p.velocity = {driftX, driftY, driftZ};
        
        // Highly opaque, hard to see through
        int colorType = rand() % 3;
        if (colorType == 0) p.color = { 100, 100, 100, 245 };
        else if (colorType == 1) p.color = { 120, 120, 120, 245 };
        else p.color = { 80, 80, 80, 245 };
        
        p.life = duration * (((float)rand() / RAND_MAX) * 0.2f + 0.9f);
        p.max_life = p.life;
        
        // OPTIMIZATION: Increase individual sphere size to compensate for fewer particles
        p.size = radius * (((float)rand() / RAND_MAX) * 0.4f + 0.6f); 
        
        particles.push_back(p);
    }
}

void ParticleManager::update(float dt) {
    for (int i = (int)particles.size() - 1; i >= 0; --i) {
        Particle& p = particles[i];
        
        p.position = Vector3Add(p.position, Vector3Scale(p.velocity, dt));
        p.life -= dt;
        
        if (p.type == ParticleType::SPARK || p.type == ParticleType::BLOOD) {
            // Gravity for sparks/blood
            p.velocity.y -= 25.0f * dt;
            // Air friction (horizontal)
            p.velocity.x *= (1.0f - 1.5f * dt);
            p.velocity.z *= (1.0f - 1.5f * dt);
            
            // Floor bounce
            if (p.position.y < 0.05f) {
                p.position.y = 0.05f;
                p.velocity.y *= (p.type == ParticleType::BLOOD ? -0.1f : -0.5f); // Blood barely bounces
                p.velocity.x *= 0.6f; // Floor friction
                p.velocity.z *= 0.6f;
            }
        } else if (p.type == ParticleType::SMOKE) {
            // Grow slowly over time
            p.size += 0.4f * dt;
        }
        
        if (p.life <= 0.0f) {
            // Swap and pop
            particles[i] = particles.back();
            particles.pop_back();
        }
    }
}

void ParticleManager::draw() const {
    for (const auto& p : particles) {
        float life_ratio = p.life / p.max_life;
        
        if (p.type == ParticleType::SPARK || p.type == ParticleType::BLOOD) {
            Color c = p.color;
            // Fade out alpha aggressively in the second half of life
            if (life_ratio < 0.5f) {
                c.a = (unsigned char)(255.0f * (life_ratio / 0.5f));
            }
            
            // Render spark as a motion-blurred streak
            float speed = Vector3Length(p.velocity);
            if (speed > 0.1f) {
                Vector3 dir = Vector3Scale(p.velocity, 1.0f / speed);
                float streak_len = speed * (p.type == ParticleType::BLOOD ? 0.02f : 0.03f); // Blood streak slightly shorter
                if (streak_len < 0.05f) streak_len = 0.05f;
                
                Vector3 start_pos = p.position;
                Vector3 end_pos = Vector3Subtract(start_pos, Vector3Scale(dir, streak_len));
                
                // DrawCylinderEx to make a 3D line with thickness that tapers off
                DrawCylinderEx(start_pos, end_pos, p.size, p.size * 0.1f, 4, c);
            } else {
                DrawCube(p.position, p.size, p.size, p.size, c);
            }
        } else if (p.type == ParticleType::SMOKE) {
            Color c = p.color;
            // Fade out smoothly, mainly at the end of its life
            if (life_ratio < 0.3f) {
                c.a = (unsigned char)((float)c.a * (life_ratio / 0.3f));
            }
            
            // OPTIMIZATION: Use DrawSphereEx to drastically reduce the polygon count.
            // Standard DrawSphere uses 16x16 segments (512 triangles).
            // This uses 8x8 segments (128 triangles) which is 4x faster per sphere.
            DrawSphereEx(p.position, p.size, 8, 8, c);
        }
    }
}
