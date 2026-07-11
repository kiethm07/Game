#pragma once
#include "raylib.h"
#include <unordered_map>

enum class GameAction {
    MOVE_FORWARD,
    MOVE_BACKWARD,
    MOVE_LEFT,
    MOVE_RIGHT,
    JUMP,
    ATTACK,
    DEFLECT,
    DODGE,
    LOCK_ON
};

enum class InputState {
    IDLE,
    PRESSED,
    HELD,
    RELEASED
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