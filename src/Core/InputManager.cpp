#include <Core/InputManager.h>
#include <raylib.h>

InputManager::InputManager() {
    registerDefaultBindings();

    action_states[GameAction::MoveForward]  = InputState::Idle;
    action_states[GameAction::MoveBackward] = InputState::Idle;
    action_states[GameAction::MoveLeft]     = InputState::Idle;
    action_states[GameAction::MoveRight]    = InputState::Idle;
    action_states[GameAction::Jump]         = InputState::Idle;
    action_states[GameAction::Attack]       = InputState::Idle;
    action_states[GameAction::Parry]        = InputState::Idle;
    action_states[GameAction::Dodge]        = InputState::Idle;
    action_states[GameAction::LockOn]       = InputState::Idle;
    action_states[GameAction::Takedown]     = InputState::Idle;
    action_states[GameAction::Crouch]       = InputState::Idle;
    action_states[GameAction::UseItem]      = InputState::Idle;
    action_states[GameAction::NextItem]     = InputState::Idle;
    action_states[GameAction::PrevItem]     = InputState::Idle;
    
    raw_mouse_delta = { 0.0f, 0.0f };
}

void InputManager::registerDefaultBindings() {
    key_bindings[KEY_W]          = GameAction::MoveForward;
    key_bindings[KEY_S]          = GameAction::MoveBackward;
    key_bindings[KEY_A]          = GameAction::MoveLeft;
    key_bindings[KEY_D]          = GameAction::MoveRight;
    key_bindings[KEY_SPACE]      = GameAction::Jump;
    key_bindings[KEY_LEFT_SHIFT] = GameAction::Dodge;
    key_bindings[KEY_F]          = GameAction::Takedown;
    key_bindings[KEY_C]          = GameAction::Crouch;
    key_bindings[KEY_X]          = GameAction::UseItem;
    key_bindings[KEY_E]          = GameAction::NextItem;
    key_bindings[KEY_Q]          = GameAction::PrevItem;

    mouse_bindings[MOUSE_BUTTON_LEFT]  = GameAction::Attack;
    mouse_bindings[MOUSE_BUTTON_RIGHT] = GameAction::Parry;
    mouse_bindings[MOUSE_BUTTON_MIDDLE]= GameAction::LockOn;
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
    if (pressed)       action_states[action] = InputState::Pressed;
    else if (down)     action_states[action] = InputState::Held;
    else if (released) action_states[action] = InputState::Released;
    else               action_states[action] = InputState::Idle;
}

bool InputManager::isActionPressed(GameAction action) const {
    auto it = action_states.find(action);
    return (it != action_states.end()) ? it->second == InputState::Pressed : false;
}

bool InputManager::isActionHeld(GameAction action) const {
    auto it = action_states.find(action);
    return (it != action_states.end()) ? it->second == InputState::Held : false;
}

bool InputManager::isActionReleased(GameAction action) const {
    auto it = action_states.find(action);
    return (it != action_states.end()) ? it->second == InputState::Released : false;
}

Vector2 InputManager::getRawMouseDelta() const {
    return raw_mouse_delta;
}