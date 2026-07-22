#include <Entities/Enemies/Swordman.h>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>

Swordman::Swordman(Vector3 start_position) 
    : Enemy(start_position) 
{
    combo = { AttackID::PlayerLight1 };
}

void Swordman::update(float dt, const Vector3& player_position) {
    combat_component.update(dt);
    stats.update(dt);

    updateAI(dt, player_position);
}

void Swordman::updateAI(float dt, const Vector3& player_position) {
    // Orient toward player
    Vector3 dir = Vector3Subtract(player_position, position);
    if (dir.x != 0.0f || dir.z != 0.0f) {
        float target_yaw = std::atan2(dir.x, dir.z) * RAD2DEG;
        rotation.y = target_yaw;
    }

    // Trigger attack when within range
    float distance = Vector3Distance(position, player_position);
    if (distance < 2.0f && combat_component.getCurrentState() == CombatState::Idle) {
        combat_component.initiateCombo(combo);
    }
}

void Swordman::draw() const {
    Color cube_color = BLUE; // Default to Idle

    switch (combat_component.getCurrentState()) {
    case CombatState::Idle:
        cube_color = BLUE;
        break;
    case CombatState::AttackStartup:
        cube_color = GOLD;       // Windup/Telegraphing phase
        break;
    case CombatState::AttackActive:
        cube_color = RED;        // Damage frames active
        break;
    case CombatState::AttackRecovery:
        cube_color = LIME;       // The open combo linking window
        break;
    case CombatState::Parrying:
        cube_color = PURPLE;     // Active deflect window
        break;
    case CombatState::Blocking:
        cube_color = DARKGRAY;   // Standing guard
        break;
    }

    // Perform matrix transformations in local space
    rlPushMatrix();

    rlTranslatef(position.x, position.y, position.z);
    rlRotatef(rotation.y, 0.0f, 1.0f, 0.0f);

    // Draw the main body with dynamic state color
    DrawCube({ 0.0f, 0.0f, 0.0f }, 1.0f, 1.0f, 1.0f, cube_color);
    DrawCubeWires({ 0.0f, 0.0f, 0.0f }, 1.0f, 1.0f, 1.0f, BLACK);

    // --- DEBUG: Draw numbers 1-6 on faces using 3D lines ---
    auto drawDebugNum = [](Vector3 center, Vector3 right, Vector3 up, int num) {
        float w = 0.15f;
        float h = 0.25f;
        Color c = RED;

        auto pt = [&](float x, float y) -> Vector3 {
            return {
                center.x + right.x * x * w + up.x * y * h,
                center.y + right.y * x * w + up.y * y * h,
                center.z + right.z * x * w + up.z * y * h
            };
            };

        Vector3 tl = pt(-1, 1); Vector3 tr = pt(1, 1);
        Vector3 ml = pt(-1, 0); Vector3 mr = pt(1, 0);
        Vector3 bl = pt(-1, -1); Vector3 br = pt(1, -1);

        if (num == 1) { DrawLine3D(pt(0, 1), pt(0, -1), c); }
        if (num == 2) { DrawLine3D(tl, tr, c); DrawLine3D(tr, mr, c); DrawLine3D(mr, ml, c); DrawLine3D(ml, bl, c); DrawLine3D(bl, br, c); }
        if (num == 3) { DrawLine3D(tl, tr, c); DrawLine3D(tr, br, c); DrawLine3D(ml, mr, c); DrawLine3D(bl, br, c); }
        if (num == 4) { DrawLine3D(tl, ml, c); DrawLine3D(ml, mr, c); DrawLine3D(tr, br, c); }
        if (num == 5) { DrawLine3D(tr, tl, c); DrawLine3D(tl, ml, c); DrawLine3D(ml, mr, c); DrawLine3D(mr, br, c); DrawLine3D(br, bl, c); }
        if (num == 6) { DrawLine3D(tr, tl, c); DrawLine3D(tl, bl, c); DrawLine3D(bl, br, c); DrawLine3D(br, mr, c); DrawLine3D(mr, ml, c); }
        };

    // Draw numbers on all 6 faces in local transformation space
    drawDebugNum({ 0.0f,  0.0f,  0.51f }, { 1.0f, 0.0f,  0.0f }, { 0.0f, 1.0f,  0.0f }, 1); // Front
    drawDebugNum({ 0.0f,  0.0f, -0.51f }, { -1.0f, 0.0f,  0.0f }, { 0.0f, 1.0f,  0.0f }, 2); // Back
    drawDebugNum({ 0.0f,  0.51f, 0.0f }, { 1.0f, 0.0f,  0.0f }, { 0.0f, 0.0f, -1.0f }, 3); // Top
    drawDebugNum({ 0.0f, -0.51f, 0.0f }, { 1.0f, 0.0f,  0.0f }, { 0.0f, 0.0f,  1.0f }, 4); // Bottom
    drawDebugNum({ 0.51f, 0.0f,  0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f,  0.0f }, 5); // Right
    drawDebugNum({ -0.51f, 0.0f,  0.0f }, { 0.0f, 0.0f,  1.0f }, { 0.0f, 1.0f,  0.0f }, 6); // Left

    rlPopMatrix();
}

HurtBox Swordman::getHurtBox() const {
    return HurtBox(position, BODY_RADIUS, this->faction, this->id);
}

std::vector<HitBox> Swordman::getActiveHitBoxes() const {
    std::vector<HitBox> active_hitboxes;

    if (combat_component.getCurrentState() == CombatState::AttackActive) {
        float yaw_rad = rotation.y * DEG2RAD;
        Vector3 forward = { std::sin(yaw_rad), 0.0f, std::cos(yaw_rad) };

        Vector3 hitbox_center = {
            position.x + forward.x * ATTACK_REACH,
            position.y,
            position.z + forward.z * ATTACK_REACH
        };

        active_hitboxes.emplace_back(
            hitbox_center,
            ATTACK_RADIUS,
            15.0f, // Health Damage
            10.0f, // Posture Damage
            getFaction(),
            getId()
        );
    }

    return active_hitboxes;
}

void Swordman::takeDamage(float health_damage, float posture_damage) {
    if (combat_component.getCurrentState() == CombatState::Parrying) return;
    if (combat_component.getCurrentState() == CombatState::Blocking) health_damage = 0.0f;

    stats.applyDamage(health_damage, posture_damage);
}