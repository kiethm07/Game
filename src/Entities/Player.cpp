#include <Entities/Player.h>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>
#include <iostream>

Player::Player(const InputManager& input_manager):
    Character(Faction::Player),
    input_manager(input_manager)
{ 
    combo = {AttackID::PlayerLight1, AttackID::PlayerLight2};
    position = {0, 0, 0};
    rotation = {0, 180.0f, 0};
}

void Player::update(float dt) {
    //Update combat component
    combat_component.update(dt);
    //Update stats component
    stats.update(dt);

    handleCombatAndUtilityInputs();
}

void Player::update(float dt, Vector3 camForward, Vector3 camRight) {
    update(dt);

    Vector3 moveDirection = calculateCameraRelativeDirection(camForward, camRight);

    if (!combat_component.canMove()) return;

    if (moveDirection.x != 0.0f || moveDirection.z != 0.0f) {
        // Apply position displacement
        position.x += moveDirection.x * MOVEMENT_SPEED * dt;
        position.z += moveDirection.z * MOVEMENT_SPEED * dt;

        // Calculate the target angle based on the horizontal direction vector
        float target_yaw = std::atan2(moveDirection.x, moveDirection.z) * RAD2DEG;

        // Calculate the shortest angular distance
        float angle_diff = target_yaw - rotation.y;
        while (angle_diff < -180.0f) angle_diff += 360.0f;
        while (angle_diff > 180.0f)  angle_diff -= 360.0f;

        // FIX 1: Safeguard the interpolation factor (alpha) against dt spikes
        float alpha = ROTATION_SPEED * dt;
        if (alpha > 1.0f) alpha = 1.0f; // Ensures it never shoots past target_yaw mathematically

        // Fluidly interpolate rotation safely
        rotation.y += angle_diff * alpha;

        // FIX 2: Keep rotation.y cleanly wrapped within a standard 0-360 range 
        // to prevent floating-point inaccuracies over time
        while (rotation.y < 0.0f) rotation.y += 360.0f;
        while (rotation.y >= 360.0f) rotation.y -= 360.0f;
    }

    //Character::update(dt);
}

void Player::draw() const{
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

    // 2. Perform matrix transformations in local space
    rlPushMatrix(); 
    
    rlTranslatef(position.x, position.y, position.z);
    rlRotatef(rotation.y, 0.0f, 1.0f, 0.0f);
    
    // Draw the main body with our dynamic state color (centered at Y = 0.5f so bottom touches ground)
    DrawCube({0.0f, 0.5f, 0.0f}, 1.0f, 1.0f, 1.0f, cube_color);
    DrawCubeWires({0.0f, 0.5f, 0.0f}, 1.0f, 1.0f, 1.0f, BLACK);

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

        Vector3 tl = pt(-1,  1); Vector3 tr = pt( 1,  1);
        Vector3 ml = pt(-1,  0); Vector3 mr = pt( 1,  0);
        Vector3 bl = pt(-1, -1); Vector3 br = pt( 1, -1);
        
        if (num == 1) { DrawLine3D(pt(0, 1), pt(0, -1), c); }
        if (num == 2) { DrawLine3D(tl, tr, c); DrawLine3D(tr, mr, c); DrawLine3D(mr, ml, c); DrawLine3D(ml, bl, c); DrawLine3D(bl, br, c); }
        if (num == 3) { DrawLine3D(tl, tr, c); DrawLine3D(tr, br, c); DrawLine3D(ml, mr, c); DrawLine3D(bl, br, c); }
        if (num == 4) { DrawLine3D(tl, ml, c); DrawLine3D(ml, mr, c); DrawLine3D(tr, br, c); }
        if (num == 5) { DrawLine3D(tr, tl, c); DrawLine3D(tl, ml, c); DrawLine3D(ml, mr, c); DrawLine3D(mr, br, c); DrawLine3D(br, bl, c); }
        if (num == 6) { DrawLine3D(tr, tl, c); DrawLine3D(tl, bl, c); DrawLine3D(bl, br, c); DrawLine3D(br, mr, c); DrawLine3D(mr, ml, c); }
    };

    // Draw numbers on all 6 faces (shifted by +0.5f in Y to match cube position)
    drawDebugNum({ 0.0f,  0.5f,  0.51f}, { 1.0f, 0.0f,  0.0f}, {0.0f, 1.0f,  0.0f}, 1); // Front
    drawDebugNum({ 0.0f,  0.5f, -0.51f}, {-1.0f, 0.0f,  0.0f}, {0.0f, 1.0f,  0.0f}, 2); // Back
    drawDebugNum({ 0.0f,  1.01f, 0.0f},  { 1.0f, 0.0f,  0.0f}, {0.0f, 0.0f, -1.0f}, 3); // Top
    drawDebugNum({ 0.0f, -0.01f, 0.0f},  { 1.0f, 0.0f,  0.0f}, {0.0f, 0.0f,  1.0f}, 4); // Bottom
    drawDebugNum({ 0.51f, 0.5f,  0.0f},  { 0.0f, 0.0f, -1.0f}, {0.0f, 1.0f,  0.0f}, 5); // Right
    drawDebugNum({-0.51f, 0.5f,  0.0f},  { 0.0f, 0.0f,  1.0f}, {0.0f, 1.0f,  0.0f}, 6); // Left
    
    rlPopMatrix(); 
}

void Player::drawHPBar2D() const {
    float bar_width = 200.0f;
    float bar_height = 16.0f;
    
    int x = 20;
    int y = GetScreenHeight() - 40; 

    float fill = stats.getHealthPercentage();

    DrawRectangle(x, y, (int)bar_width, (int)bar_height, DARKGRAY);
    DrawRectangle(x, y, (int)(bar_width * fill), (int)bar_height, LIME);
    DrawRectangleLines(x, y, (int)bar_width, (int)bar_height, WHITE);
}

float Player::getColliderRadius() const {
    return BODY_RADIUS;
}

float Player::getColliderHeight() const {
    return BODY_HEIGHT;
}

std::vector<HurtBox> Player::getHurtBoxes() const {
    // Generate an upright 3D body capsule centered at position
    Capsule body_capsule = Capsule::createUpright(position, BODY_HEIGHT, BODY_RADIUS);
    return { HurtBox(body_capsule, getFaction(), getId()) };
}

std::vector<HitBox> Player::getActiveHitBoxes() const {
    std::vector<HitBox> active_hitboxes;

    if (combat_component.getCurrentState() == CombatState::AttackActive) {
        float yaw_rad = rotation.y * DEG2RAD;
        Vector3 forward = { std::sin(yaw_rad), 0.0f, std::cos(yaw_rad) };

        // Position attack sphere in front of the player at chest height
        Vector3 hitbox_center = {
            position.x + forward.x * ATTACK_REACH,
            position.y + (BODY_HEIGHT * 0.5f), 
            position.z + forward.z * ATTACK_REACH
        };

        Sphere attack_sphere(hitbox_center, ATTACK_RADIUS);

        active_hitboxes.emplace_back(
            attack_sphere,
            25.0f, // Health damage
            15.0f, // Posture damage
            getFaction(),
            getId()
        );
    }

    return active_hitboxes;
}

void Player::takeDamage(float health_damage, float posture_damage) {
    // 1. Guard check state machine windows
    if (combat_component.getCurrentState() == CombatState::Parrying) {
        // Perfect deflect window: Ignore damage entirely!
        return;
    }

    if (combat_component.getCurrentState() == CombatState::Blocking) {
        // Blocking cuts HP damage in half, but takes full posture damage
        health_damage = 0.0f;
    }

    // 2. Pass straight to Stats!
    bool hit_applied = stats.applyDamage(health_damage, posture_damage);

    if (hit_applied) {
        if (stats.isPostureBroken()) {
            // Stance broken state!
        } else if (stats.isDead()) {
            // Player death state!
        }
    }
}

Vector3 Player::calculateCameraRelativeDirection(Vector3 camForward, Vector3 camRight) const {
    camForward.y = 0.0f;
    camRight.y = 0.0f;
    
    //Remove y for calculation
    camForward = Vector3Normalize(camForward);
    camRight = Vector3Normalize(camRight);

    Vector3 direction = { 0.0f, 0.0f, 0.0f };
    
    if (input_manager.isActionHeld(GameAction::MoveForward))  direction = Vector3Add(direction, camForward);
    if (input_manager.isActionHeld(GameAction::MoveBackward)) direction = Vector3Subtract(direction, camForward);
    if (input_manager.isActionHeld(GameAction::MoveRight))    direction = Vector3Add(direction, camRight);
    if (input_manager.isActionHeld(GameAction::MoveLeft))     direction = Vector3Subtract(direction, camRight);

    if (direction.x != 0.0f || direction.z != 0.0f) {
        direction = Vector3Normalize(direction);
    }
    return direction;
}

void Player::handleCombatAndUtilityInputs() {
    if (input_manager.isActionPressed(GameAction::Attack)) {
        combat_component.initiateCombo(combo);
    }
    if (input_manager.isActionPressed(GameAction::Parry)) {
        combat_component.startGuard();
    }
    if (input_manager.isActionReleased(GameAction::Parry)) {
        combat_component.stopGuard();
    }
    if (input_manager.isActionPressed(GameAction::Dodge)) {
    }
    if (input_manager.isActionPressed(GameAction::LockOn)) {
    }
}