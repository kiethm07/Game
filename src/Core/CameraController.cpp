#include <Core/CameraController.h>
#include <math.h>

CameraController::CameraController() {
    // Initialize Raylib Camera struct
    camera.position = { 0.0f, 0.0f, 0.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 60.0f; // A slightly wider FOV feels better for action games
    camera.projection = CAMERA_PERSPECTIVE;

    // Default orbital settings
    distance = 5.0f;     // 5 units behind the player
    pitch = 20.0f;       // Looking slightly down at the player
    yaw = 0.0f;          // Looking straight ahead
    sensitivity = 0.2f;  // Adjust this to make the mouse feel right
}

void CameraController::Update(Vector3 targetPosition, Vector2 mouseDelta) {
    // 1. Adjust angles based on raw mouse input
    yaw -= mouseDelta.x * sensitivity;
    pitch += mouseDelta.y * sensitivity;

    // 2. Clamp the pitch to prevent the camera from flipping upside down
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    // 3. Convert degrees to radians for C++ math functions
    float pitchRad = pitch * DEG2RAD;
    float yawRad = yaw * DEG2RAD;

    // 4. Calculate the Camera's position using Spherical to Cartesian math
    // This orbits the camera around the target based on the yaw and pitch
    camera.position.x = targetPosition.x + distance * cosf(pitchRad) * sinf(yawRad);
    camera.position.y = targetPosition.y + distance * sinf(pitchRad);
    camera.position.z = targetPosition.z + distance * cosf(pitchRad) * cosf(yawRad);

    // 5. Tell the camera to look exactly at the target (the player)
    // We add a slight Y offset so the camera looks at the player's chest/head, not their feet
    camera.target = { targetPosition.x, targetPosition.y + 1.0f, targetPosition.z };
}

Camera3D CameraController::GetCamera() const {
    return camera;
}