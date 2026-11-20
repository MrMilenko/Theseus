// console_mode.cpp: console mode's state and the guide's leave and power
// options (console_mode.h).

#include "console_mode.h"
#include "host.h"

#include <SDL.h>
#include <stdio.h>

extern SDL_Window* g_pSDLWindow;
extern bool g_displayChangeRequested;
void SaveDesktopSettings();

bool g_consoleMode = false;
bool g_consoleFlag = false;

bool Console_Active() { return g_consoleMode || g_consoleFlag; }

void Console_GoToDesktop()
{
    if (!g_pSDLWindow) return;
    // A fullscreen window won't minimize everywhere, so drop it first. The
    // restore puts it back.
    SDL_SetWindowFullscreen(g_pSDLWindow, 0);
    SDL_MinimizeWindow(g_pSDLWindow);
}

bool Console_Power(int action)
{
    SaveDesktopSettings();
    fflush(stdout);
    fflush(stderr);
    return Host_Power(action);
}
