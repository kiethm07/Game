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

    bool canMove() const;
    bool isHitboxActive() const;
    CombatState getCurrentState() const;
private:
    CombatState current_state;
    float state_timer;
    const Combo* active_combo_ptr;
    int combo_index;

    void startAttackPhase();
    void resetToIdle();
};