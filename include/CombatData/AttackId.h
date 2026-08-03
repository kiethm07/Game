#pragma once

enum class AttackID {
    PlayerLight1,
    PlayerLight2,
    PlayerHeavyFinisher,

    /// The deathblow. Not part of any combo — it is played on its own by
    /// Player::performTakedown(), which is the only thing that reaches it.
    PlayerExecution
};