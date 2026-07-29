#pragma once
#include "raylib.h"
#include <CombatData/Combo.h>
#include <CombatData/AttackRegistry.h>

enum class CombatState {
    Idle,
    AttackStartup,
    AttackActive,
    AttackRecovery,
    Parrying,
    Blocking,
    Dodging
};

class CombatComponent {
public:
    CombatComponent();
    ~CombatComponent() = default;
    
    void update(float dt);
    void initiateCombo(const Combo& combo, bool auto_advance = false);
    void startGuard();
    void stopGuard();

    /// Enters the dodge state for `duration` seconds. The caller passes the
    /// dodge clip's length so the state ends exactly when the animation does —
    /// the authored clip is the source of truth for how long a dodge lasts,
    /// not a hand-typed constant that drifts out of sync with it.
    bool startDodge(float duration);

    bool canMove() const;
    bool canDodge() const;
    bool isHitboxActive() const;
    CombatState getCurrentState() const;

    /// The attack currently being performed, or nullptr outside attack states.
    /// Lets the owner drive animation and root motion from the same frame data
    /// the state machine is timing against.
    const AttackData* getActiveAttack() const;

    /// Counter bumped every time a deliberate action begins — a swing, a guard
    /// raise, a dodge. Several states share one clip (all three attack phases;
    /// the parry window and the block hold), so "acted again" is invisible to a
    /// state comparison: a combo step that reuses the previous step's animation
    /// leaves the state unchanged, as does re-raising a guard that is already
    /// up. This is what lets the owner tell those apart from "still in the same
    /// action" and rewind the clip only for the former.
    unsigned getActionId() const;

    private:
    //Core
    CombatState current_state = CombatState::Idle;
    float state_timer = 0;
    
    //Guard
    bool is_guard_held = false;
    bool canGuard() const;
    const float DEFAULT_PARRY_WINDOW = 0.20f;
    //const float PARRY_PENALTY_WINDOW = 0.10f;

    //Attack
    const Combo* active_combo_ptr = nullptr;
    int combo_index = 0;
    bool is_auto_combo = false;
    unsigned action_id = 0;
    void startAttackPhase();
    void resetToIdle();
};