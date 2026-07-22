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
    Blocking
};

class CombatComponent {
public:
    CombatComponent();
    ~CombatComponent() = default;
    
    void update(float dt);
    void initiateCombo(const Combo& combo);
    void startGuard();
    void stopGuard();

    bool canMove() const;
    bool isHitboxActive() const;
    CombatState getCurrentState() const;
    
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
    const Combo* active_combo_ptr;
    int combo_index;
    void startAttackPhase();
    void resetToIdle();
};