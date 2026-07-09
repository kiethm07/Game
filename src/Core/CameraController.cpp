#include <Core/CameraController.h>
#include <math.h>
#include <raymath.h>

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

void CameraController::update(Vector3 target_position, Vector2 mouse_delta) {
    // 1. Adjust angles based on raw mouse input
    yaw -= mouse_delta.x * sensitivity;
    pitch += mouse_delta.y * sensitivity;

    // 2. Clamp the pitch to prevent the camera from flipping upside down
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    // 3. Convert degrees to radians for C++ math functions
    float pitchRad = pitch * DEG2RAD;
    float yawRad = yaw * DEG2RAD;

    // 4. Calculate the Camera's position using Spherical to Cartesian math
    // This orbits the camera around the target based on the yaw and pitch
    camera.position.x = target_position.x + distance * cosf(pitchRad) * sinf(yawRad);
    camera.position.y = target_position.y + distance * sinf(pitchRad);
    camera.position.z = target_position.z + distance * cosf(pitchRad) * cosf(yawRad);

    // 5. Tell the camera to look exactly at the target (the player)
    // We add a slight Y offset so the camera looks at the player's chest/head, not their feet
    camera.target = { target_position.x, target_position.y + 1.0f, target_position.z };
}

Camera3D CameraController::getCamera() const {
    return camera;
}

Vector3 CameraController::getCameraForward() const {
    // Calculate the forward vector from the camera's target and position
    Vector3 forward = Vector3Subtract(camera.target, camera.position);
    // Normalize the vector to make it a unit vector
    forward = Vector3Normalize(forward);
    return forward;
}

Vector3 CameraController::getCameraRight() const {
    // Calculate the right vector from the camera's forward and up vectors
    Vector3 right = Vector3CrossProduct(getCameraForward(), camera.up);
    // Normalize the vector to make it a unit vector
    right = Vector3Normalize(right);
    return right;
}