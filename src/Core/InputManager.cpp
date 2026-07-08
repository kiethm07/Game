#include "Core/InputManager.h"

InputManager::InputManager() {
    RegisterDefaultBindings();

    actionStates[GameAction::MOVE_FORWARD]  = InputState::IDLE;
    actionStates[GameAction::MOVE_BACKWARD] = InputState::IDLE;
    actionStates[GameAction::MOVE_LEFT]     = InputState::IDLE;
    actionStates[GameAction::MOVE_RIGHT]    = InputState::IDLE;
    actionStates[GameAction::JUMP]          = InputState::IDLE;
    actionStates[GameAction::ATTACK]        = InputState::IDLE;
    actionStates[GameAction::DEFLECT]       = InputState::IDLE;
    actionStates[GameAction::DODGE]         = InputState::IDLE;
    actionStates[GameAction::LOCK_ON]       = InputState::IDLE;
    
    rawMouseDelta = { 0.0f, 0.0f };
}

void InputManager::RegisterDefaultBindings() {
    keyBindings[KEY_W]          = GameAction::MOVE_FORWARD;
    keyBindings[KEY_S]          = GameAction::MOVE_BACKWARD;
    keyBindings[KEY_A]          = GameAction::MOVE_LEFT;
    keyBindings[KEY_D]          = GameAction::MOVE_RIGHT;
    keyBindings[KEY_SPACE]      = GameAction::JUMP;
    keyBindings[KEY_LEFT_SHIFT] = GameAction::DODGE;
    keyBindings[KEY_F]          = GameAction::LOCK_ON;

    mouseBindings[MOUSE_BUTTON_LEFT]  = GameAction::ATTACK;
    mouseBindings[MOUSE_BUTTON_RIGHT] = GameAction::DEFLECT;
}

void InputManager::Update() {
    // Simply pass the raw hardware delta straight through. 
    // It's not "camera delta" yet; it's just mouse speed.
    rawMouseDelta = GetMouseDelta();
    PollBindings();
}

void InputManager::PollBindings() {
    for (const auto& [key, action] : keyBindings) {
        UpdateBindingState(action, IsKeyPressed(key), IsKeyDown(key), IsKeyReleased(key));
    }
    for (const auto& [button, action] : mouseBindings) {
        UpdateBindingState(action, IsMouseButtonPressed(button), IsMouseButtonDown(button), IsMouseButtonReleased(button));
    }
}

void InputManager::UpdateBindingState(GameAction action, bool pressed, bool down, bool released) {
    if (pressed)       actionStates[action] = InputState::PRESSED;
    else if (down)     actionStates[action] = InputState::HELD;
    else if (released) actionStates[action] = InputState::RELEASED;
    else               actionStates[action] = InputState::IDLE;
}

bool InputManager::IsActionPressed(GameAction action) const {
    auto it = actionStates.find(action);
    return (it != actionStates.end()) ? it->second == InputState::PRESSED : false;
}

bool InputManager::IsActionHeld(GameAction action) const {
    auto it = actionStates.find(action);
    return (it != actionStates.end()) ? it->second == InputState::HELD : false;
}

bool InputManager::IsActionReleased(GameAction action) const {
    auto it = actionStates.find(action);
    return (it != actionStates.end()) ? it->second == InputState::RELEASED : false;
}

Vector2 InputManager::GetRawMouseDelta() const {
    return rawMouseDelta;
}