#pragma once

#include <States/StateAction.h>

class GameState{
public:
    virtual ~GameState() = default;

    virtual void enter() = 0;
    virtual StateAction update(float dt) = 0;
    virtual void draw() = 0;
    virtual void exit() = 0;
};