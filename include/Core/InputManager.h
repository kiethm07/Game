#pragma once
#include "raylib.h"
#include <unordered_map>

enum class GameAction {
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    Jump,
    Attack,
    Parry,
    Dodge,
    LockOn
};

enum class InputState {
    Idle,
    Pressed,
    Held,
    Released
};

class InputManager {
public:
    InputManager();
    ~InputManager() = default;

    void update();

    // Pure current-frame snapshot queries
    bool isActionPressed(GameAction action) const;
    bool isActionHeld(GameAction action) const;
    bool isActionReleased(GameAction action) const;

    // Raw, unprocessed hardware mouse movement
    Vector2 getRawMouseDelta() const;

private:
    void registerDefaultBindings();
    void pollBindings();
    void updateBindingState(GameAction action, bool pressed, bool down, bool released);

    std::unordered_map<int, GameAction> key_bindings;
    std::unordered_map<int, GameAction> mouse_bindings;
    std::unordered_map<GameAction, InputState> action_states;

    Vector2 raw_mouse_delta;
};