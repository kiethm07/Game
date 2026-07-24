#include <Entities/Enemy.h>
#include <raymath.h>
#include <rlgl.h>

Enemy::Enemy(Vector3 start_position, Faction faction)
    : Character(faction)
{
    position = start_position;
    rotation = { 0.0f, 0.0f, 0.0f };
}

void Enemy::draw() const {
    if (stats.isDead()) return;

    Color cube_color = base_color;
    switch (combat_component.getCurrentState()) {
        case CombatState::AttackStartup:  cube_color = GOLD; break;
        case CombatState::AttackActive:   cube_color = RED; break;
        case CombatState::AttackRecovery: cube_color = LIME; break;
        case CombatState::Parrying:       cube_color = PURPLE; break;
        case CombatState::Blocking:       cube_color = DARKGRAY; break;
        default:                          cube_color = base_color; break;
    }

    rlPushMatrix();
    rlTranslatef(position.x, position.y, position.z);
    rlRotatef(rotation.y, 0.0f, 1.0f, 0.0f);

    float half_h = visual_size.y * 0.5f;

    DrawCube({ 0.0f, half_h, 0.0f }, visual_size.x, visual_size.y, visual_size.z, cube_color);
    DrawCubeWires({ 0.0f, half_h, 0.0f }, visual_size.x, visual_size.y, visual_size.z, BLACK);

    // Debug numbers 1-6 on cube faces
    auto drawDebugNum = [](Vector3 center, Vector3 right, Vector3 up, int num) {
        float w = 0.15f, h = 0.25f;
        Color c = RED;

        auto pt = [&](float x, float y) -> Vector3 {
            return {
                center.x + right.x * x * w + up.x * y * h,
                center.y + right.y * x * w + up.y * y * h,
                center.z + right.z * x * w + up.z * y * h
            };
        };

        Vector3 tl = pt(-1, 1), tr = pt(1, 1);
        Vector3 ml = pt(-1, 0), mr = pt(1, 0);
        Vector3 bl = pt(-1, -1), br = pt(1, -1);

        if (num == 1) { DrawLine3D(pt(0, 1), pt(0, -1), c); }
        if (num == 2) { DrawLine3D(tl, tr, c); DrawLine3D(tr, mr, c); DrawLine3D(mr, ml, c); DrawLine3D(ml, bl, c); DrawLine3D(bl, br, c); }
        if (num == 3) { DrawLine3D(tl, tr, c); DrawLine3D(tr, br, c); DrawLine3D(ml, mr, c); DrawLine3D(bl, br, c); }
        if (num == 4) { DrawLine3D(tl, ml, c); DrawLine3D(ml, mr, c); DrawLine3D(tr, br, c); }
        if (num == 5) { DrawLine3D(tr, tl, c); DrawLine3D(tl, ml, c); DrawLine3D(ml, mr, c); DrawLine3D(mr, br, c); DrawLine3D(br, bl, c); }
        if (num == 6) { DrawLine3D(tr, tl, c); DrawLine3D(tl, bl, c); DrawLine3D(bl, br, c); DrawLine3D(br, mr, c); DrawLine3D(mr, ml, c); }
    };

    drawDebugNum({ 0.0f,  half_h,  0.51f }, { 1.0f, 0.0f,  0.0f }, { 0.0f, 1.0f,  0.0f }, 1);
    drawDebugNum({ 0.0f,  half_h, -0.51f }, { -1.0f, 0.0f,  0.0f }, { 0.0f, 1.0f,  0.0f }, 2);
    drawDebugNum({ 0.0f,  half_h + 0.51f, 0.0f }, { 1.0f, 0.0f,  0.0f }, { 0.0f, 0.0f, -1.0f }, 3);
    drawDebugNum({ 0.0f, half_h - 0.51f, 0.0f }, { 1.0f, 0.0f,  0.0f }, { 0.0f, 0.0f,  1.0f }, 4);
    drawDebugNum({ 0.51f, half_h,  0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f,  0.0f }, 5);
    drawDebugNum({ -0.51f, half_h,  0.0f }, { 0.0f, 0.0f,  1.0f }, { 0.0f, 1.0f,  0.0f }, 6);

    rlPopMatrix();
}

void Enemy::drawHPBar(const Camera3D& camera) const {
    if (stats.isDead()) return;

    Vector3 head_pos = { position.x, position.y + body_height + 0.2f, position.z };

    Vector3 cam_forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 to_enemy = Vector3Subtract(head_pos, camera.position);

    if (Vector3DotProduct(cam_forward, to_enemy) <= 0.0f) return;

    Vector2 screen_pos = GetWorldToScreen(head_pos, camera);

    if (screen_pos.x < 0 || screen_pos.x > GetScreenWidth() ||
        screen_pos.y < 0 || screen_pos.y > GetScreenHeight()) {
        return;
    }

    float width = 60.0f;
    float height = 6.0f;
    int x = static_cast<int>(screen_pos.x - (width / 2.0f));
    int y = static_cast<int>(screen_pos.y);

    DrawRectangle(x, y, (int)width, (int)height, RED);
    DrawRectangle(x, y, (int)(width * stats.getHealthPercentage()), (int)height, LIME);
    DrawRectangleLines(x, y, (int)width, (int)height, BLACK);
}

float Enemy::getColliderRadius() const {
    return body_radius;
}

float Enemy::getColliderHeight() const {
    return body_height;
}

std::vector<HurtBox> Enemy::getHurtBoxes() const {
    if (stats.isDead()) return {};

    Capsule body_capsule = Capsule::createUpright(position, body_height, body_radius);
    return { HurtBox(body_capsule, getFaction(), getId()) };
}

void Enemy::takeDamage(float health_damage, float posture_damage) {
    if (combat_component.getCurrentState() == CombatState::Parrying) return;
    if (combat_component.getCurrentState() == CombatState::Blocking) health_damage = 0.0f;

    stats.applyDamage(health_damage, posture_damage);
}