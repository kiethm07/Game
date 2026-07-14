#include <Components/CombatComponent.h>

CombatComponent::CombatComponent() {
    resetToIdle();
}

void CombatComponent::startAttackPhase() {
    if (!active_combo_ptr || combo_index >= active_combo_ptr->getAttackCount()) {
        resetToIdle();
        return;
    }

    AttackID current_id = active_combo_ptr->getAttackID(combo_index);
    
    const AttackData& frame_data = AttackRegistry::instance().getAttackData(current_id);
    
    current_state = CombatState::AttackStartup;
    state_timer = frame_data.getStartupDuration();
}

void CombatComponent::resetToIdle() {
    current_state = CombatState::Idle;
    state_timer = 0.0f;
    active_combo_ptr = nullptr;
    combo_index = 0;
}

void CombatComponent::update(float dt) {
    if (current_state == CombatState::Idle || current_state == CombatState::Blocking) {
        return;
    }

    state_timer -= dt;

    if (state_timer <= 0.0f) {
            AttackID current_id = active_combo_ptr->getAttackID(combo_index);
            const AttackData& frame_data = AttackRegistry::instance().getAttackData(current_id);

            if (current_state == CombatState::AttackStartup) {
                current_state = CombatState::AttackActive;
                state_timer = frame_data.getActiveDuration();
            } 
            else if (current_state == CombatState::AttackActive) {
                current_state = CombatState::AttackRecovery;
                state_timer = frame_data.getRecoveryDuration();
            } 
            else if (current_state == CombatState::AttackRecovery) {
                resetToIdle();
            }
    }
}

void CombatComponent::initiateCombo(const Combo& combo) {
    if (combo.isEmpty()) return;

    if (current_state == CombatState::Idle) {
        active_combo_ptr = &combo; 
        combo_index = 0;
        startAttackPhase();
    } 
    else if (current_state == CombatState::AttackRecovery && active_combo_ptr == &combo) {
        combo_index++;
        
        if (combo_index < active_combo_ptr->getAttackCount()) {
            startAttackPhase();
        } else {
            resetToIdle();
        }
    }
}

bool CombatComponent::canMove() const {
    return current_state == CombatState::Idle || current_state == CombatState::Blocking;
}

bool CombatComponent::isHitboxActive() const {
    return current_state == CombatState::AttackActive;
}

CombatState CombatComponent::getCurrentState() const {
    return current_state;
}