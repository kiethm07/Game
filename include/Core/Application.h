#pragma once

#include <Core/Game.h>
#include <raylib.h>

class Application{
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();
private:
    Game game;
};