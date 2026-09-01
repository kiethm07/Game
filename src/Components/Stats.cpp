#include <Components/Stats.h>
#include <cmath>

void Stats::update(float dt, bool is_holding_block) {
    // Recover posture over time back to 0
    if (current_posture > 0.0f) {
        time_since_last_damage += dt;
        
        float current_regen_rate = 0.0f;
        float hp_ratio = getHealthPercentage();

        if (is_holding_block) {
            // Holding guard: 1.0s delay, no HP penalty, balanced logarithmic pace
            if (time_since_last_damage > 1.0f) {
                float time_past_delay = time_since_last_damage - 1.0f;
                float log_scale = 1.0f + std::log(1.0f + 1.2f * time_past_delay);
                current_regen_rate = posture_regen_rate * 1.75f * log_scale;
            }
        } else if (hp_ratio > 0.40f) {
            // Standard unblocked recovery: 3.0s delay with HP ratio scaling
            if (time_since_last_damage > 3.0f) {
                float normalized_hp = (hp_ratio - 0.40f) / 0.60f; // Maps 40%-100% HP to 0.0-1.0
                float inverse_hp = 1.0f - normalized_hp;
                float hp_multiplier = 1.0f - (inverse_hp * inverse_hp);

                float time_past_delay = time_since_last_damage - 3.0f;
                float base_rate = posture_regen_rate * (time_past_delay * 0.5f);
                current_regen_rate = base_rate * hp_multiplier;
            }
        }

        current_posture -= current_regen_rate * dt;
        if (current_posture < 0.0f) {
            current_posture = 0.0f;
        }
    } else {
        time_since_last_damage = 0.0f; // Reset when fully recovered
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

void Stats::recoverPosture(float amount) {
    if (amount <= 0.0f) {
        return;
    }
    current_posture -= amount;
    if (current_posture < 0.0f) {
        current_posture = 0.0f;
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
    } else if (current_health > max_health) {
        current_health = max_health;
    }

    current_posture += posture_damage;
    if (current_posture > max_posture) {
        current_posture = max_posture;
    }

    if (health_damage > 0.0f || posture_damage > 0.0f) {
        time_since_last_damage = 0.0f; // Reset regeneration timer on hit
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
