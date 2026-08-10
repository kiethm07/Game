#pragma once

enum class StateAction{
    KeepCurrent,
    ChangeToMenu,
    ChangeToLoading,
    ChangeToGameplay,
    RequestQuit
};