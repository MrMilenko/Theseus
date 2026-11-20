// console_mode.cpp: console mode's state (console_mode.h).

#include "console_mode.h"

bool g_consoleMode = false;
bool g_consoleFlag = false;

bool Console_Active() { return g_consoleMode || g_consoleFlag; }
