#pragma once

enum class AttackID {
    PlayerLight1,
    PlayerLight2,
    PlayerHeavyFinisher,

    /// The deathblow. Not part of any combo — it is played on its own by
    /// Player::performTakedown(), which is the only thing that reaches it.
    PlayerExecution,

    /// The mini boss's rotation, in the order Swordman walks through it. Each
    /// is one AttackData playing one clip from the greatsword pack, and the two
    /// combos carry their swings INSIDE that one clip -- see SwingWindow, and
    /// AttackRegistry for where each window's timing was measured.
    MiniBossSwing,       ///< `Attack_H`, one horizontal cut.
    MiniBossDoubleSwing, ///< `Combo_3`, two cuts.
    MiniBossTripleSwing  ///< `Combo_2`, two cuts and an overhead chop.
};
