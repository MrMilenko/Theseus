// pads.h: the controllers (pads.cpp). Pads can come and go any time; the
// one pressed last is the active pad.
#pragma once
#include <SDL.h>

void Pads_HandleEvent(const SDL_Event* event);  // pads connecting and leaving
void Pads_Update();                             // once a frame, after the events
void Pads_Shutdown();

int  Pads_Count();
SDL_GameController* Pads_Get(int i);
// The pad pressed last, NULL with none connected. Things only one player
// can hold, like a stream, go to this one.
SDL_GameController* Pads_Active();

// Every pad drives the dashboard: a button counts held on any pad, and a
// stick or trigger reads off whichever pad pushes it furthest.
bool   Pads_Button(SDL_GameControllerButton button);
Sint16 Pads_Axis(SDL_GameControllerAxis axis);

// Which labels the buttons get, in ui_overlay's UI_PAD_* order: the
// active pad's own, unless ButtonStyle picks one.
enum { PAD_STYLE_XBOX, PAD_STYLE_PLAYSTATION, PAD_STYLE_NINTENDO };
int  Pads_Style();
extern int g_padStyle;     // desktop.ini ButtonStyle: 0 the pad's own, else style + 1

// A short buzz on the active pad, strength 0 to 1. Nothing with Rumble off.
void Pads_Rumble(float strength, Uint32 ms);
extern bool g_padRumble;   // desktop.ini Rumble
