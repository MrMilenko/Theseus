// console_mode.h: UIX Desktop standing in for the machine's own front end.
// Fullscreen, no frame, straight into the dashboard. The OS shell stays
// where it was underneath.
#pragma once

extern bool g_consoleMode;   // desktop.ini ConsoleMode
extern bool g_consoleFlag;   // --console, this run only

// Either of the two.
bool Console_Active();

// Minimizes the window. It comes back fullscreen when it's restored.
void Console_GoToDesktop();
// Saves settings and flushes the log, then asks the host (host.h HOST_POWER_).
bool Console_Power(int action);
