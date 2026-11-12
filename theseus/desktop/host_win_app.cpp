// host_win_app.cpp: the process half of host.h for Windows. Like
// host_win.cpp, this may see <windows.h> and must never see std.h.

#include "host.h"

#include <SDL.h>
#include <SDL_syswm.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <dbghelp.h>
#include <avrt.h>
#include <dwmapi.h>

#include <io.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

void Host_ConsoleOrLog(const char* logPath) {
	// /SUBSYSTEM:WINDOWS so explorer launches don't pop a console, but
	// attach to a parent shell when one exists so stdout/stderr show up.
	if (AttachConsole(ATTACH_PARENT_PROCESS)) {
		freopen("CONOUT$", "w", stdout);
		freopen("CONOUT$", "w", stderr);
		freopen("CONIN$", "r", stdin);
		return;
	}
	// Launched from Explorer, so there's no console and every fprintf goes
	// nowhere. Tee to a log the user can actually send back.
	char dir[MAX_PATH];
	snprintf(dir, sizeof(dir), "%s", logPath);
	char* slash = strrchr(dir, '/');
	char* back = strrchr(dir, '\\');
	if (back > slash) slash = back;
	if (slash) {
		*slash = '\0';
		Host_MakeDir(dir);
	}
	if (freopen(logPath, "w", stdout)) {
		setvbuf(stdout, NULL, _IOLBF, 0);
		_dup2(_fileno(stdout), _fileno(stderr));
	}
}

void Host_BoostProcess() {
	//Milenko-Testing: I don't run windows, i googled this. don't know if it will work as intended.
	SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
	DwmEnableMMCSS(TRUE);
	{
		DWORD taskIdx = 0;
		AvSetMmThreadCharacteristicsW(L"Games", &taskIdx);
	}
	typedef BOOL (WINAPI *SetProcessInfoFn)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
	HMODULE hKernel = GetModuleHandleA("kernel32.dll");
	if (hKernel) {
		SetProcessInfoFn pSetProcessInformation =
			(SetProcessInfoFn)GetProcAddress(hKernel, "SetProcessInformation");
		if (pSetProcessInformation) {
			PROCESS_POWER_THROTTLING_STATE pt = {0};
			pt.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
			pt.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
			pt.StateMask = 0; // 0 == opt out of throttling
			pSetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &pt, sizeof(pt));
		}
	}
}

static LONG WINAPI WindowsCrashHandler(EXCEPTION_POINTERS* ep) {
	fprintf(stderr, "\n[CRASH] Exception 0x%08lX at address 0x%p\n",
	        ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
	if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
		fprintf(stderr, "[CRASH] %s address 0x%p\n",
		        ep->ExceptionRecord->ExceptionInformation[0] ? "Writing" : "Reading",
		        (void*)ep->ExceptionRecord->ExceptionInformation[1]);
	}
	// Walk the stack
	HANDLE process = GetCurrentProcess();
	SymInitialize(process, NULL, TRUE);
	STACKFRAME64 frame = {};
	CONTEXT* ctx = ep->ContextRecord;
	frame.AddrPC.Offset = ctx->Rip;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = ctx->Rbp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = ctx->Rsp;
	frame.AddrStack.Mode = AddrModeFlat;
	fprintf(stderr, "[CRASH] Stack trace:\n");
	for (int i = 0; i < 32; i++) {
		if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, ctx, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
			break;
		char symBuf[sizeof(SYMBOL_INFO) + 256];
		SYMBOL_INFO* sym = (SYMBOL_INFO*)symBuf;
		sym->SizeOfStruct = sizeof(SYMBOL_INFO);
		sym->MaxNameLen = 255;
		DWORD64 disp = 0;
		if (SymFromAddr(process, frame.AddrPC.Offset, &disp, sym))
			fprintf(stderr, "  [%d] %s + 0x%llx\n", i, sym->Name, (unsigned long long)disp);
		else
			fprintf(stderr, "  [%d] 0x%p\n", i, (void*)frame.AddrPC.Offset);
	}
	fflush(stderr);
	return EXCEPTION_EXECUTE_HANDLER;
}

static void AbortHandler(int sig) {
	fprintf(stderr, "\n[CRASH] Signal %d (%s)\n", sig, sig == SIGABRT ? "SIGABRT" : "OTHER");
	fflush(stderr);
	exit(128 + sig);
}

void Host_InstallCrashHandler() {
	SetUnhandledExceptionFilter(WindowsCrashHandler);
	signal(SIGABRT, AbortHandler);
}

bool Host_NativeWindow(SDL_Window* window, void** nwh, void** ndt, bool* wayland) {
	*nwh = NULL;
	*ndt = NULL;
	*wayland = false;
	SDL_SysWMinfo wmi;
	SDL_VERSION(&wmi.version);
	if (!SDL_GetWindowWMInfo(window, &wmi)) {
		fprintf(stderr, "SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
		return false;
	}
	*nwh = wmi.info.win.window;
	return true;
}

// URLs go through ShellExecute. Raw commands run through cmd /C; no /S, so
// cmd preserves outer quotes for the 2-quote/exists case and strips
// first/last otherwise.
bool Host_Spawn(const char* spec, bool isUrl, bool isSteamUrl, const char* workdir,
                char* err, size_t errSize) {
	(void)isSteamUrl;
	if (isUrl) {
		HINSTANCE rc = ShellExecuteA(NULL, "open", spec, NULL, workdir, SW_SHOWNORMAL);
		if ((INT_PTR)rc <= 32) {
			snprintf(err, errSize, "ShellExecute failed (code %ld): %s", (long)(INT_PTR)rc, spec);
			return false;
		}
		return true;
	}

	STARTUPINFOA si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};
	char cmd[2048];
	// Quoted .exe spec skips cmd entirely. CreateProcessA with
	// lpApplicationName=NULL parses the first quoted token as the exe with
	// no shell quote-mangling. cmd's strip-first-and-last rule eats our
	// outer wrapper otherwise. .bat / .cmd / etc. stay on the cmd path
	// because they need a shell to interpret.
	bool directExe = false;
	if (spec[0] == '"') {
		const char* exeEnd = strchr(spec + 1, '"');
		if (exeEnd && exeEnd - spec >= 5 && _strnicmp(exeEnd - 4, ".exe", 4) == 0)
			directExe = true;
	}
	if (directExe) {
		strncpy(cmd, spec, sizeof(cmd) - 1);
		cmd[sizeof(cmd) - 1] = 0;
	} else {
		snprintf(cmd, sizeof(cmd), "cmd /C \"%s\"", spec);
	}
	if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, DETACHED_PROCESS, NULL, workdir, &si, &pi)) {
		DWORD e = GetLastError();
		snprintf(err, errSize, "CreateProcess failed (error %lu): %s", e, spec);
		return false;
	}
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return true;
}

bool Host_PickFile(const char* title, char* out, size_t outSize) {
	OPENFILENAMEA ofn = {};
	char path[MAX_PATH] = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFile   = path;
	ofn.nMaxFile    = MAX_PATH;
	ofn.lpstrTitle  = title;
	ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameA(&ofn)) return false;
	snprintf(out, outSize, "%s", path);
	return true;
}
