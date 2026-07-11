#include "Core/InputManager.h"

InputManager::InputManager() {
    registerDefaultBindings();

    action_states[GameAction::MOVE_FORWARD]  = InputState::IDLE;
    action_states[GameAction::MOVE_BACKWARD] = InputState::IDLE;
    action_states[GameAction::MOVE_LEFT]     = InputState::IDLE;
    action_states[GameAction::MOVE_RIGHT]    = InputState::IDLE;
    action_states[GameAction::JUMP]          = InputState::IDLE;
    action_states[GameAction::ATTACK]        = InputState::IDLE;
    action_states[GameAction::DEFLECT]       = InputState::IDLE;
    action_states[GameAction::DODGE]         = InputState::IDLE;
    action_states[GameAction::LOCK_ON]       = InputState::IDLE;
    
    raw_mouse_delta = { 0.0f, 0.0f };
}

void InputManager::registerDefaultBindings() {
    key_bindings[KEY_W]          = GameAction::MOVE_FORWARD;
    key_bindings[KEY_S]          = GameAction::MOVE_BACKWARD;
    key_bindings[KEY_A]          = GameAction::MOVE_LEFT;
    key_bindings[KEY_D]          = GameAction::MOVE_RIGHT;
    key_bindings[KEY_SPACE]      = GameAction::JUMP;
    key_bindings[KEY_LEFT_SHIFT] = GameAction::DODGE;
    key_bindings[KEY_F]          = GameAction::LOCK_ON;

    mouse_bindings[MOUSE_BUTTON_LEFT]  = GameAction::ATTACK;
    mouse_bindings[MOUSE_BUTTON_RIGHT] = GameAction::DEFLECT;
}

void InputManager::update() {
    raw_mouse_delta = GetMouseDelta();
    pollBindings();
}

void InputManager::pollBindings() {
    for (const auto& [key, action] : key_bindings) {
        updateBindingState(action, IsKeyPressed(key), IsKeyDown(key), IsKeyReleased(key));
    }
    for (const auto& [button, action] : mouse_bindings) {
        updateBindingState(action, IsMouseButtonPressed(button), IsMouseButtonDown(button), IsMouseButtonReleased(button));
    }
}

void InputManager::updateBindingState(GameAction action, bool pressed, bool down, bool released) {
    if (pressed)       action_states[action] = InputState::PRESSED;
    else if (down)     action_states[action] = InputState::HELD;
    else if (released) action_states[action] = InputState::RELEASED;
    else               action_states[action] = InputState::IDLE;
}

bool InputManager::isActionPressed(GameAction action) const {
    auto it = action_states.find(action);
    return (it != action_states.end()) ? it->second == InputState::PRESSED : false;
}

bool InputManager::isActionHeld(GameAction action) const {
    auto it = action_states.find(action);
    return (it != action_states.end()) ? it->second == InputState::HELD : false;
}

bool InputManager::isActionReleased(GameAction action) const {
    auto it = action_states.find(action);
    return (it != action_states.end()) ? it->second == InputState::RELEASED : false;
}

Vector2 InputManager::getRawMouseDelta() const {
    return raw_mouse_delta;
}