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

    void Update();

    // Pure current-frame snapshot queries
    bool IsActionPressed(GameAction action) const;
    bool IsActionHeld(GameAction action) const;
    bool IsActionReleased(GameAction action) const;

    // Raw, unprocessed hardware mouse movement
    Vector2 GetRawMouseDelta() const;

private:
    void RegisterDefaultBindings();
    void PollBindings();
    void UpdateBindingState(GameAction action, bool pressed, bool down, bool released);

    std::unordered_map<int, GameAction> keyBindings;
    std::unordered_map<int, GameAction> mouseBindings;
    std::unordered_map<GameAction, InputState> actionStates;

    Vector2 rawMouseDelta;
};