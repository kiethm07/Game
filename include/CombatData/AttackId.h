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
    ///
    /// The spin took the single cut's place in the rotation. `Attack_H`, the
    /// clip that cut played, is still in the pack and still measured (the
    /// window was 0.90 + 0.20 + 1.31, one sweep at 1.30 m); nothing plays it.
    MiniBossSpin,        ///< `Attack_Spin`, a cock-back, a hold, three turns.
    MiniBossDoubleSwing, ///< `Combo_3`, two cuts.
    MiniBossTripleSwing, ///< `Combo_2`, two cuts and an overhead chop.

    /// The final boss's rotation. Same shape as the mini boss's -- one
    /// AttackData per clip, with multi-hit clips carrying their swings inside
    /// through SwingWindow -- but timed off the FISTS rather than a blade,
    /// because this character has no weapon.
    FinalBossPunch,      ///< `Attack`, a right jab and a left club swing.
    FinalBossFlurry,     ///< `Attack_Rapid`, five alternating swipes.
    FinalBossLeap,       ///< `Attack_Jump`, a 2.56 m leap, a slam and two swings.

    /// The kimono swordsman's rotation. Same shape again -- one AttackData per
    /// clip, multi-hit clips carrying their swings through SwingWindow -- timed
    /// off the KATANA TIP tracked through each clip, which is what the brief's
    /// two combo strings turned into once measured.
    KimonoSwing,         ///< `Attack`, one downward diagonal cut.
    KimonoCleave,        ///< `Combo_1`, two cuts and an overhead into the ground.
    KimonoLunge          ///< `Combo_2`, a step-in cut, a sweep and a rising slash.
};
