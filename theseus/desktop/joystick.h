// joystick.h: desktop CJoystick declarations. Companion to
// desktop/joystick.cpp.

#pragma once
#include "node.h"
#include <SDL.h>

class CJoystick : public CNode
{
public:
    static CJoystick* c_pBoundJoystick;
    static CJoystick* c_pPreviousBoundJoystick;
    static SDL_GameController* c_controller;

    void Bind(); // this binds to itself

    void OnMoveUp();
    void OnMoveDown();
    void OnADown();
    void OnBDown();
};
