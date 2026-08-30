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
    Dodging,
    PostureBroken,
    BeingExecuted
};

class CombatComponent {
public:
    CombatComponent();
    ~CombatComponent() = default;
    
    void update(float dt);
    void initiateCombo(const Combo& combo, bool auto_advance = false);

    /// Raises the guard, opening the parry window.
    ///
    /// `held` is whether the guard button is still down at the moment the guard
    /// actually goes up, which is not always the frame it was asked for: a
    /// press latched during an attack's recovery is honoured when the recovery
    /// ends, and by then a quick tap is long over. Passing the button's real
    /// state there is what makes such a tap a deflect that lapses on its own,
    /// rather than a block left standing with nothing holding it up.
    void startGuard(bool held = true);
    void stopGuard();

    /// A deflect that actually caught something. Clears the spam penalty, so
    /// the next guard gets the full parry window however soon it comes.
    ///
    /// This is what makes the penalty a punishment for mashing at nothing
    /// rather than a cap on how fast a guard may be re-raised: a combo thrown
    /// faster than PARRY_SPAM_COOLDOWN can be deflected hit for hit, and only
    /// a swing at empty air costs the player their next window.
    void notifyParrySuccess();

    /// Enters the dodge state for `duration` seconds. The caller passes the
    /// dodge clip's length so the state ends exactly when the animation does —
    /// the authored clip is the source of truth for how long a dodge lasts,
    /// not a hand-typed constant that drifts out of sync with it.
    bool startDodge(float duration);

    /// Abandons whatever action is running and returns to Idle. For events
    /// outside the combat machine's own timeline that make the current action
    /// moot — a landing stagger arriving mid-swing, whose hitbox must not stay
    /// live through a recovery the player no longer controls.
    void interrupt();

    /// Forces the component into the PostureBroken state for the given duration,
    /// blocking movement and actions until it recovers.
    void breakPosture(float duration);

    void setBeingExecuted() { current_state = CombatState::BeingExecuted; }

    bool canMove() const;
    bool canDodge() const;
    bool canGuard() const;
    bool isHitboxActive() const;

    /// True for both halves of a guard — the parry window and the block hold
    /// that follows it. They share one animation and one set of movement rules,
    /// so callers asking "is the guard up" want both.
    bool isGuarding() const;

    CombatState getCurrentState() const;

    /// The attack currently being performed, or nullptr outside attack states.
    /// Lets the owner drive animation and root motion from the same frame data
    /// the state machine is timing against.
    const AttackData* getActiveAttack() const;

    /// Which of the active attack's hit windows the machine is timing against.
    ///
    /// Always 0 for an attack that is a single swing, which is every attack but
    /// the mini boss's two combos. The owner needs it because the hitboxes are
    /// per swing: asking an attack for "its" hitboxes is not a question with
    /// one answer once a clip carries three of them.
    int getActiveSwing() const;

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
    const float DEFAULT_PARRY_WINDOW = 0.20f;
    const float PARRY_PENALTY_WINDOW = 0.05f;

    /// How long a *missed* deflect shortens the next one. Deliberately longer
    /// than the interval of any combo the player is expected to deflect —
    /// notifyParrySuccess() clears it, so the only way to still be inside it is
    /// to have guarded at nothing.
    const float PARRY_SPAM_COOLDOWN = 0.50f;
    float spam_timer = 0.0f;

    //Attack
    const Combo* active_combo_ptr = nullptr;
    int combo_index = 0;

    /// Which hit window of the CURRENT attack is being timed. An attack with
    /// several of them runs Startup -> Active -> Startup -> Active -> ... and
    /// only then Recovery, staying one attack throughout: `action_id` is not
    /// bumped between windows, because the animator rewinds the clip on that id
    /// and the whole point of a combo clip is that it keeps playing across its
    /// own swings.
    int swing_index = 0;

    bool is_auto_combo = false;
    unsigned action_id = 0;
    void startAttackPhase();
    void resetToIdle();
};