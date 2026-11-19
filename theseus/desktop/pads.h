// pads.h: the controllers (pads.cpp). Pads can come and go any time; the
// one pressed last is the active pad.
#pragma once
#include <SDL.h>

void Pads_HandleEvent(const SDL_Event* event);  // pads connecting and leaving
void Pads_Update();                             // once a frame, after the events
void Pads_Shutdown();

int  Pads_Count();
// The pad pressed last, NULL with none connected.
SDL_GameController* Pads_Active();
