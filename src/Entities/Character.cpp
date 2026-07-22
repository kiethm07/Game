#include <Entities/Character.h>
#include <rlgl.h>

Character::Character(Faction faction) : faction(faction) {
    id = next_id++;
    position = {0, 0, 0};
    rotation = {0, -180, 0};
}

void Character::draw() const{
    rlPushMatrix(); //Record the current world matrix
    
    rlTranslatef(position.x, position.y, position.z);
    
    rlRotatef(rotation.y, 0.0f, 1.0f, 0.0f);
    
    DrawCube({0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f, BLUE);
    DrawCubeWires({0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f, BLACK);

    // --- DEBUG: Draw numbers 1-6 on faces using 3D lines ---
    // A quick lambda to draw simple 7-segment style numbers in local 3D space
    auto drawDebugNum = [](Vector3 center, Vector3 right, Vector3 up, int num) {
        float w = 0.15f; // Half-width of the number
        float h = 0.25f; // Half-height of the number
        Color c = RED;   // High-contrast debug color

        // Helper lambda to map flat 2D offsets onto our 3D face alignment
        auto pt = [&](float x, float y) -> Vector3 {
            return {
                center.x + right.x * x * w + up.x * y * h,
                center.y + right.y * x * w + up.y * y * h,
                center.z + right.z * x * w + up.z * y * h
            };
        };

        // Key points for a standard digital number layout
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

    // Draw numbers on all 6 faces 
    // (Centers are pushed slightly past 0.5f to prevent the lines from clipping into the cube mesh)
    drawDebugNum({ 0.0f,  0.0f,  0.51f}, { 1.0f, 0.0f,  0.0f}, {0.0f, 1.0f,  0.0f}, 1); // Front
    drawDebugNum({ 0.0f,  0.0f, -0.51f}, {-1.0f, 0.0f,  0.0f}, {0.0f, 1.0f,  0.0f}, 2); // Back
    drawDebugNum({ 0.0f,  0.51f, 0.0f},  { 1.0f, 0.0f,  0.0f}, {0.0f, 0.0f, -1.0f}, 3); // Top
    drawDebugNum({ 0.0f, -0.51f, 0.0f},  { 1.0f, 0.0f,  0.0f}, {0.0f, 0.0f,  1.0f}, 4); // Bottom
    drawDebugNum({ 0.51f, 0.0f,  0.0f},  { 0.0f, 0.0f, -1.0f}, {0.0f, 1.0f,  0.0f}, 5); // Right
    drawDebugNum({-0.51f, 0.0f,  0.0f},  { 0.0f, 0.0f,  1.0f}, {0.0f, 1.0f,  0.0f}, 6); // Left
    // ------------------------------------------------------
    
    rlPopMatrix(); //Restore the previous world matrix
}