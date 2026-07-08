#include <Core/InputManager.h>

#include <cmath>

InputManager::InputManager() {
    RegisterDefaultBindings();

    // Initialize all tracked gameplay actions to a known idle state.
    actionStates[GameAction::JUMP] = InputState::IDLE;
    actionStates[GameAction::ATTACK] = InputState::IDLE;
    actionStates[GameAction::DEFLECT] = InputState::IDLE;
    actionStates[GameAction::DODGE] = InputState::IDLE;
    actionStates[GameAction::LOCK_ON] = InputState::IDLE;
}

void InputManager::RegisterDefaultBindings() {
    // The raw hardware layer stays here. Game logic only sees GameAction values.
    keyBindings[KEY_SPACE] = GameAction::JUMP;
    keyBindings[KEY_LEFT_SHIFT] = GameAction::DODGE;
    keyBindings[KEY_F] = GameAction::LOCK_ON;

    mouseBindings[MOUSE_BUTTON_LEFT] = GameAction::ATTACK;
    mouseBindings[MOUSE_BUTTON_RIGHT] = GameAction::DEFLECT;
}

void InputManager::Update() {
    cameraDelta = GetMouseDelta();
    UpdateMovementVector();
    PollBindings();
    CleanupExpiredInputs();
}

void InputManager::PollBindings() {
    for (const auto& binding : keyBindings) {
        const int key = binding.first;
        const GameAction action = binding.second;

        UpdateBindingState(action, IsKeyPressed(key), IsKeyDown(key), IsKeyReleased(key));
    }

    for (const auto& binding : mouseBindings) {
        const int button = binding.first;
        const GameAction action = binding.second;

        UpdateBindingState(action, IsMouseButtonPressed(button), IsMouseButtonDown(button), IsMouseButtonReleased(button));
    }
}

void InputManager::UpdateBindingState(GameAction action, bool pressed, bool down, bool released) {
    InputState nextState = InputState::IDLE;

    if (pressed) {
        nextState = InputState::PRESSED;
        inputBuffer.emplace_back(action, ActionData{InputState::PRESSED, static_cast<float>(GetTime())});
        
    } else if (down) {
        nextState = InputState::HELD;
    } else if (released) {
        nextState = InputState::RELEASED;
    }

    actionStates[action] = nextState;
}

void InputManager::UpdateMovementVector() {
    Vector2 rawMovement{0.0f, 0.0f};

    if (IsKeyDown(KEY_D)) {
        rawMovement.x += 1.0f;
    }

    if (IsKeyDown(KEY_A)) {
        rawMovement.x -= 1.0f;
    }

    if (IsKeyDown(KEY_W)) {
        rawMovement.y += 1.0f;
    }

    if (IsKeyDown(KEY_S)) {
        rawMovement.y -= 1.0f;
    }

    const float length = std::sqrt((rawMovement.x * rawMovement.x) + (rawMovement.y * rawMovement.y));
    if (length > 0.0f) {
        rawMovement.x /= length;
        rawMovement.y /= length;
    }

    movementVector = rawMovement;
}

void InputManager::CleanupExpiredInputs() {
    const float now = static_cast<float>(GetTime());

    // The buffer is FIFO, so only the front can be stale first.
    // Removing old entries here prevents a buffered action from firing long after the player intended it.
    while (!inputBuffer.empty()) {
        const auto& front = inputBuffer.front();
        if ((now - front.second.timestamp) <= INPUT_BUFFER_TTL) {
            break;
        }

        inputBuffer.pop_front();
    }
}

bool InputManager::ConsumeAction(GameAction action) {
    if (inputBuffer.empty()) {
        return false;
    }

    const float now = static_cast<float>(GetTime());
    const auto& front = inputBuffer.front();

    // Only the front of the queue is eligible for consumption so input order stays deterministic.
    if ((now - front.second.timestamp) > INPUT_BUFFER_TTL) {
        inputBuffer.pop_front();
        return false;
    }

    if (front.first != action) {
        return false;
    }

    inputBuffer.pop_front();
    return true;
}

bool InputManager::IsActionHeld(GameAction action) const {
    const auto it = actionStates.find(action);
    if (it == actionStates.end()) {
        return false;
    }

    return it->second == InputState::HELD;
}

void InputManager::Flush() {
    inputBuffer.clear();
}

Vector2 InputManager::GetMovementVector() const {
    return movementVector;
}

Vector2 InputManager::GetCameraDelta() const {
    return cameraDelta;
}