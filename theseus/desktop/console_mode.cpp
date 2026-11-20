// console_mode.cpp: console mode's state and the guide's leave and power
// options (console_mode.h).

#include "console_mode.h"
#include "host.h"
#include "app_paths.h"

#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>

extern SDL_Window* g_pSDLWindow;
extern bool g_displayChangeRequested;
void SaveDesktopSettings();

bool g_consoleMode = false;
bool g_consoleFlag = false;
bool g_consoleSupervised = false;

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

// What a child's exit code asks the watchdog for. Anything else nonzero is a crash.
enum { RELAUNCH_SAME = 75, RELAUNCH_DASHBOARD, RELAUNCH_DEVELOPMENT };

// The watchdog's own lines go to the terminal and the end of theseus.log.
// The child is gone by then, and its next run keeps this log as theseus.prev.log.
static void WatchdogLog(const char* fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fprintf(stderr, "[Console] %s\n", line);
    char path[1024];
    snprintf(path, sizeof(path), "%s/theseus.log", Plat_UserDataDir());
    if (FILE* f = fopen(path, "a")) {
        fprintf(f, "[Console] %s\n", line);
        fclose(f);
    }
}

static bool IsModeFlag(const char* a)
{
    return !strcmp(a, "--dashboard") || !strcmp(a, "--development") || !strcmp(a, "--preview");
}

bool Console_Supervise(int argc, char** argv, int* code)
{
    bool console = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--supervised")) g_consoleSupervised = true;
        if (!strcmp(argv[i], "--console")) console = true;
    }
    if (!console || g_consoleSupervised) return false;

    std::vector<std::string> args(argv, argv + argc);
    args.push_back("--supervised");
    time_t lastCrash = 0;
    for (;;) {
        std::vector<const char*> list;
        for (size_t i = 0; i < args.size(); i++) list.push_back(args[i].c_str());
        list.push_back(NULL);

        int exitCode = 0;
        char how[128];
        int outcome = Host_RunSelf(list.data(), &exitCode, how, sizeof(how));
        if (outcome == HOST_CHILD_FAILED) {
            WatchdogLog("couldn't start UIX Desktop");
            *code = 1;
            return true;
        }
        if (outcome == HOST_CHILD_EXITED && exitCode == 0) {
            *code = 0;
            return true;
        }
        if (outcome == HOST_CHILD_EXITED && exitCode >= RELAUNCH_SAME && exitCode <= RELAUNCH_DEVELOPMENT) {
            if (exitCode != RELAUNCH_SAME) {
                std::vector<std::string> kept;
                for (size_t i = 0; i < args.size(); i++)
                    if (!IsModeFlag(args[i].c_str())) kept.push_back(args[i]);
                kept.push_back(exitCode == RELAUNCH_DASHBOARD ? "--dashboard" : "--development");
                args.swap(kept);
            }
            WatchdogLog("restarting UIX Desktop");
            continue;
        }
        time_t now = time(NULL);
        if (lastCrash && now - lastCrash < 60) {
            WatchdogLog("UIX Desktop crashed again within a minute (%s). Not starting it again.", how);
            *code = 1;
            return true;
        }
        lastCrash = now;
        WatchdogLog("UIX Desktop crashed (%s). Starting it again.", how);
    }
}

void Console_RestartViaWatchdog(const char* mode)
{
    if (!g_consoleSupervised || Host_ExecKeepsProcess()) return;
    int code = !mode ? RELAUNCH_SAME : !strcmp(mode, "--development") ? RELAUNCH_DEVELOPMENT : RELAUNCH_DASHBOARD;
    fflush(stdout);
    fflush(stderr);
    // Like exec: nothing else runs on the way out.
    _Exit(code);
}
