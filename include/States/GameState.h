#pragma once

#include <States/StateAction.h>

class GameState{
public:
    virtual ~GameState() = default;

    virtual void Enter() = 0;
    virtual StateAction Update(float dt) = 0;
    virtual void Draw() = 0;
    virtual void Exit() = 0;
};