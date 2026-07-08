#pragma once
#include <raylib.h>
#include <raymath.h>

class CameraController {
public:
    CameraController();
    ~CameraController() = default;

    // Call this once per frame, passing the player's position and the mouse movement
    void Update(Vector3 targetPosition, Vector2 mouseDelta);

    // Returns the internal Raylib camera for drawing
    Camera3D GetCamera() const;

private:
    Camera3D camera;

    // Orbital parameters
    float distance;
    float pitch; // Up/Down angle
    float yaw;   // Left/Right angle

    // Mouse sensitivity
    float sensitivity;
};