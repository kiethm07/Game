#pragma once

class Stats {
public:
    Stats(float max_health = 100.0f, float max_posture = 100.0f, float posture_regen_rate = 15.0f)
        : current_health(max_health),
          max_health(max_health),
          current_posture(0.0f),
          max_posture(max_posture),
          posture_regen_rate(posture_regen_rate),
          time_since_last_damage(0.0f),
          is_invincible(false),
          invincibility_timer(0.0f) {}

    ~Stats() = default;

    void update(float dt);
    bool applyDamage(float health_damage, float posture_damage);
    void triggerInvincibility(float duration);
    void setInvincible(bool status);

    float getCurrentHealth() const { return current_health; }
    float getMaxHealth() const { return max_health; }
    float getHealthPercentage() const { return (max_health > 0.0f) ? (current_health / max_health) : 0.0f; }
    bool isDead() const { return current_health <= 0.0f; }

    float getCurrentPosture() const { return current_posture; }
    float getMaxPosture() const { return max_posture; }
    float getPosturePercentage() const { return (max_posture > 0.0f) ? (current_posture / max_posture) : 0.0f; }
    bool isPostureBroken() const { return current_posture >= max_posture; }
    void resetPosture() { current_posture = 0.0f; }

    bool getIsInvincible() const { return is_invincible; }

private:
    float current_health;
    float max_health;

    float current_posture;
    float max_posture;
    float posture_regen_rate;
    float time_since_last_damage;

    bool is_invincible;
    float invincibility_timer;
};