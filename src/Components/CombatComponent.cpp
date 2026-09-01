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
    swing_index = 0;
    state_timer = frame_data.getWindupDuration(0);
    action_id++;
}

void CombatComponent::resetToIdle() {
    current_state = CombatState::Idle;
    state_timer = 0.0f;
    active_combo_ptr = nullptr;
    combo_index = 0;
    swing_index = 0;
    is_guard_held = false;
}

void CombatComponent::update(float dt) {
    if (spam_timer > 0.0f) {
        spam_timer -= dt;
    }

    if (current_state == CombatState::Idle) {
        return;
    }

    //Dodge & PostureBroken — fixed-length committed states; their displacement comes from the
    //clip's root motion, so nothing here needs to know how far they travel.
    if (current_state == CombatState::Dodging || current_state == CombatState::PostureBroken) {
        state_timer -= dt;
        if (state_timer <= 0.0f) {
            resetToIdle();
        }
        return;
    }

    if (current_state == CombatState::BeingExecuted) {
        return;
    }

    //Guard (Parry + Block)
    if (current_state == CombatState::Parrying) {
        state_timer -= dt;
        
        if (state_timer <= 0.0f) {
            if (is_guard_held) {
                current_state = CombatState::Blocking;
            } else {
                resetToIdle();
            }
        }
        return; 
    }

    if (current_state == CombatState::Blocking) {
        if (!is_guard_held) {
            resetToIdle();
        }
        return;
    }

    //Attack
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
            if (is_auto_combo && combo_index + 1 < active_combo_ptr->getAttackCount()) {
                combo_index++;
                startAttackPhase();
            } else {
                resetToIdle();
            }
        }
        return;
    }
}

bool CombatComponent::initiateCombo(const Combo& combo, bool auto_advance) {
    if (combo.isEmpty()) {
        return false;
    }

    if (current_state == CombatState::Idle) {
        active_combo_ptr = &combo; 
        combo_index = 0;
        is_auto_combo = auto_advance;
        startAttackPhase();
        return true;
    } 
    else if (current_state == CombatState::AttackRecovery && active_combo_ptr == &combo) {
        if (combo_index + 1 < active_combo_ptr->getAttackCount()) {
            combo_index++;
            startAttackPhase();
            return true;
        }
    }
    return false;
}

void CombatComponent::startGuard(bool held) {
    if (!canGuard()) {
        is_guard_held = false;
        return;
    }

    is_guard_held = held;

    // Cancelling out of an attack, in either of the two phases canGuard()
    // allows: the swing is abandoned, so the combo it belonged to must not
    // survive the guard and be resumed by the next initiateCombo(). Never the
    // active frames — those own a live hitbox, and canGuard() refuses them.
    if (current_state == CombatState::AttackStartup ||
        current_state == CombatState::AttackRecovery) {
        active_combo_ptr = nullptr;
        combo_index = 0;
        swing_index = 0;
    }

    current_state = CombatState::Parrying;
    state_timer = (spam_timer > 0.0f) ? PARRY_PENALTY_WINDOW : DEFAULT_PARRY_WINDOW;
    spam_timer = PARRY_SPAM_COOLDOWN;
    action_id++;
}

void CombatComponent::notifyParrySuccess() {
    // Armed by startGuard() before it can possibly be known whether the guard
    // was going to catch anything. Landing the deflect is what retracts it.
    spam_timer = 0.0f;
}

void CombatComponent::stopGuard() {
    is_guard_held = false;
    if (current_state == CombatState::Parrying) {
        //Do nothing
    }
    else if (current_state == CombatState::Blocking) {
        resetToIdle();
    }  
}

bool CombatComponent::startDodge(float duration) {
    if (!canDodge() || duration <= 0.0f) return false;

    active_combo_ptr = nullptr;
    combo_index = 0;
    swing_index = 0;
    is_guard_held = false;
    current_state = CombatState::Dodging;
    state_timer = duration;
    action_id++;
    return true;
}

void CombatComponent::breakPosture(float duration) {
    if (duration <= 0.0f) return;
    
    active_combo_ptr = nullptr;
    combo_index = 0;
    swing_index = 0;
    is_guard_held = false;
    current_state = CombatState::PostureBroken;
    state_timer = duration;
    action_id++;
}

void CombatComponent::interrupt() { resetToIdle(); }

bool CombatComponent::canMove() const {
    return current_state == CombatState::Idle
        || current_state == CombatState::Blocking;
}

bool CombatComponent::canDodge() const {
    // Cancellable out of a guard or an attack's recovery, but not out of an
    // attack already committed to startup or active frames.
    return current_state == CombatState::Idle
        || current_state == CombatState::Blocking
        || current_state == CombatState::Parrying
        || current_state == CombatState::AttackRecovery;
}

const AttackData* CombatComponent::getActiveAttack() const {
    if (!active_combo_ptr) return nullptr;
    if (current_state != CombatState::AttackStartup &&
        current_state != CombatState::AttackActive &&
        current_state != CombatState::AttackRecovery) {
        return nullptr;
    }
    if (combo_index >= active_combo_ptr->getAttackCount()) return nullptr;

    return &AttackRegistry::instance().getAttackData(
        active_combo_ptr->getAttackID(combo_index));
}

int CombatComponent::getActiveSwing() const { return swing_index; }

unsigned CombatComponent::getActionId() const { return action_id; }

bool CombatComponent::canGuard() const {
    // Recovery is cancellable, matching canDodge(). A swing's recovery is the
    // longest phase there is, so a guard that had to wait it out would be
    // refused for most of the time the player spends attacking — which is
    // exactly when the enemy is answering.
    return current_state == CombatState::Idle 
        || current_state == CombatState::Parrying
        || current_state == CombatState::Blocking
        || current_state == CombatState::AttackStartup
        || current_state == CombatState::AttackRecovery;
}

bool CombatComponent::isGuarding() const {
    return current_state == CombatState::Parrying
        || current_state == CombatState::Blocking;
}

bool CombatComponent::isHitboxActive() const {
    return current_state == CombatState::AttackActive;
}

CombatState CombatComponent::getCurrentState() const {
    return current_state;
}