// console_mode.h: UIX Desktop standing in for the machine's own front end.
// Fullscreen, no frame, straight into the dashboard. The OS shell stays
// where it was underneath.
#pragma once

extern bool g_consoleMode;   // desktop.ini ConsoleMode
extern bool g_consoleFlag;   // --console, this run only
extern bool g_consoleSupervised;  // --supervised: a watchdog started us

// Either of the two.
bool Console_Active();

// Minimizes the window. It comes back fullscreen when it's restored.
void Console_GoToDesktop();
// Saves settings and flushes the log, then asks the host (host.h HOST_POWER_).
bool Console_Power(int action);

// --console starts a small watchdog first: it runs the app as its child
// (adding --supervised) and starts it again after a crash, unless it
// crashed twice within a minute. True when this process was the watchdog
// and main should return code.
bool Console_Supervise(int argc, char** argv, int* code);
// Under the watchdog where exec can't keep the process, a restart is the
// watchdog's job: this exits asking for one. mode is the new mode flag, or
// NULL for the same. Returns when it isn't the case.
void Console_RestartViaWatchdog(const char* mode);
