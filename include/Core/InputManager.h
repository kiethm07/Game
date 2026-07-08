#pragma once

#include <raylib.h>

#include <deque>
#include <unordered_map>
#include <utility>

// High-level gameplay actions that stay stable even if the raw hardware bindings change.
enum class GameAction {
	JUMP,
	ATTACK,
	DEFLECT,
	DODGE,
	LOCK_ON,
};

// Per-action state used by game logic. PRESSED is emitted on the first frame only,
// HELD is emitted while the input remains down, and RELEASED is emitted once on release.
enum class InputState {
	IDLE,
	PRESSED,
	HELD,
	RELEASED,
};

// Timestamped action metadata stored in the buffer so gameplay can consume inputs
// with deterministic timing rules instead of depending on immediate hardware state.
struct ActionData {
	InputState state = InputState::IDLE;
	float timestamp = 0.0f;
};

class InputManager {
public:
	InputManager();
	~InputManager() = default;

	InputManager(const InputManager&) = delete;
	InputManager& operator=(const InputManager&) = delete;

	void Update();
	bool ConsumeAction(GameAction action);
	bool IsActionHeld(GameAction action) const;
	void Flush();

	Vector2 GetMovementVector() const;
	Vector2 GetCameraDelta() const;

private:
	struct GameActionHash {
		std::size_t operator()(GameAction action) const noexcept {
			return std::hash<int>{}(static_cast<int>(action));
		}
	};

	using ActionStateMap = std::unordered_map<GameAction, InputState, GameActionHash>;
	using InputBindingMap = std::unordered_map<int, GameAction>;
	using InputBuffer = std::deque<std::pair<GameAction, ActionData>>;

	void RegisterDefaultBindings();
	void PollBindings();
	void UpdateBindingState(GameAction action, bool pressed, bool down, bool released);
	void UpdateMovementVector();
	void CleanupExpiredInputs();

	static constexpr float INPUT_BUFFER_TTL = 0.0f;

	ActionStateMap actionStates;
	InputBindingMap keyBindings;
	InputBindingMap mouseBindings;
	InputBuffer inputBuffer;

	Vector2 movementVector = {0.0f, 0.0f};
	Vector2 cameraDelta = {0.0f, 0.0f};
};
