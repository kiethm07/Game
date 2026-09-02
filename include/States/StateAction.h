#pragma once

/// What a state asks Game to do next, returned by GameState::update.
///
/// Deliberately payload-free. A state cannot touch the state stack — it returns
/// one of these by value and Game acts on it after update() has returned, which
/// is what makes "a state replaces itself" safe without a deferred-destruction
/// queue. Anything a transition needs to know beyond "what kind" lives in a
/// service Game owns, not in here.
///
/// Two families, and the distinction is load-bearing: ChangeToX names a
/// destination the state knows, RequestX names an intent whose destination it
/// does not. RequestNextPhase is the second kind — only Campaign knows whether
/// there is a next phase to go to or whether the run is over.
enum class StateAction{
    KeepCurrent,
    ChangeToMenu,
    ChangeToLoading,
    ChangeToGameplay,
    RequestNextPhase,
    RequestReloadPhase,
    RequestQuit,
    PushPause,
    PopPause,
    PushSetting,
    PopSetting
};
