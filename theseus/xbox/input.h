// input.h: CJoystick XAP node. Wraps the active gamepad and routes
// directional / button events to scripts via OnMoveUp / OnADown /
// etc. Companion to xbox/input.cpp.

#pragma once
#include "node.h"

class CJoystick : public CNode
{
public:
    static CJoystick* c_pActivePad;
    static CJoystick* c_pLastActivePad;

    void Bind();

    void OnMoveUp();
    void OnMoveDown();
    void OnADown();
    void OnBDown();
};

