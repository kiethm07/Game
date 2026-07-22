#include <Components/Stats.h>

void Stats::update(float dt) {
    // Recover posture over time back to 0
    if (current_posture > 0.0f) {
        current_posture -= posture_regen_rate * dt;
        if (current_posture < 0.0f) {
            current_posture = 0.0f;
        }
    }

    // Countdown temporary invincibility (e.g. dodge i-frames)
    if (invincibility_timer > 0.0f) {
        invincibility_timer -= dt;
        if (invincibility_timer <= 0.0f) {
            invincibility_timer = 0.0f;
            is_invincible = false;
        }
    }
}

// Applies damage and returns true if hit connected (returns false if invincible)
bool Stats::applyDamage(float health_damage, float posture_damage) {
    if (is_invincible) {
        return false;
    }

    current_health -= health_damage;
    if (current_health < 0.0f) {
        current_health = 0.0f;
    }

    current_posture += posture_damage;
    if (current_posture > max_posture) {
        current_posture = max_posture;
    }

    return true;
}

// Invincibility Controls
void Stats::triggerInvincibility(float duration) {
    is_invincible = true;
    invincibility_timer = duration;
}

void Stats::setInvincible(bool status) {
    is_invincible = status;
    if (!status) {
        invincibility_timer = 0.0f;
    }
}
