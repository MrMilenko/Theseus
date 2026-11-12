// host_win_app.cpp: the process half of host.h for Windows. Like
// host_win.cpp, this may see <windows.h> and must never see std.h.

#include "host.h"
#include "host_crash.h"

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
#include <winioctl.h>
#include <ntddcdrm.h>

#include <io.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

// theseus.log when we write one, for the crash report.
static char s_logPath[MAX_PATH];

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
	char prev[MAX_PATH];
	snprintf(prev, sizeof(prev), "%s", logPath);
	char* ext = strstr(prev, ".log");
	if (ext && (size_t)(ext - prev) + sizeof(".prev.log") <= sizeof(prev))
		strcpy(ext, ".prev.log");
	MoveFileExA(logPath, prev, MOVEFILE_REPLACE_EXISTING);
	if (freopen(logPath, "w", stdout)) {
		setvbuf(stdout, NULL, _IOLBF, 0);
		_dup2(_fileno(stdout), _fileno(stderr));
		snprintf(s_logPath, sizeof(s_logPath), "%s", logPath);
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

// Crash reports. An unhandled exception filter writes crashes\<local time>.txt
// in the user data folder (version, OS, exception, stack walk, the end of
// theseus.log, home folder shown as ~) and a minidump beside it. Follows
// MrBruh's approach: abort() raises an exception of its own so the filter
// sees it too, and the main thread keeps stack room for after an overflow.
#define CRASH_ABORT_EXCEPTION 0xE0000001UL

static char s_crashDir[MAX_PATH];
static char s_crashHome[MAX_PATH];
static char s_crashHeader[1024];
static CrashText s_report, s_redacted;
static char s_logTail[CRASH_LOG_READ];

static const char* ExceptionName(DWORD code) {
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION:      return "EXCEPTION_ACCESS_VIOLATION";
	case EXCEPTION_STACK_OVERFLOW:        return "EXCEPTION_STACK_OVERFLOW";
	case EXCEPTION_ILLEGAL_INSTRUCTION:   return "EXCEPTION_ILLEGAL_INSTRUCTION";
	case EXCEPTION_PRIV_INSTRUCTION:      return "EXCEPTION_PRIV_INSTRUCTION";
	case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "EXCEPTION_INT_DIVIDE_BY_ZERO";
	case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
	case EXCEPTION_IN_PAGE_ERROR:         return "EXCEPTION_IN_PAGE_ERROR";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
	case EXCEPTION_BREAKPOINT:            return "EXCEPTION_BREAKPOINT";
	case 0xC0000409:                      return "STATUS_STACK_BUFFER_OVERRUN";
	case CRASH_ABORT_EXCEPTION:           return "abort()";
	default:                              return "exception";
	}
}

// "theseus.exe + 0x1a2b3c", plus a symbol name when dbghelp has one.
static void CrashFrame(CrashText* t, HANDLE process, int idx, DWORD64 addr) {
	CrashAppend(t, "  #");
	CrashAppendDec(t, (uint64_t)idx, 2);
	CrashAppend(t, "  ");
	MEMORY_BASIC_INFORMATION mbi;
	char path[MAX_PATH];
	if (VirtualQuery((void*)(ULONG_PTR)addr, &mbi, sizeof(mbi)) && mbi.Type == MEM_IMAGE && mbi.AllocationBase &&
	    GetModuleFileNameA((HMODULE)mbi.AllocationBase, path, MAX_PATH)) {
		path[MAX_PATH - 1] = '\0';
		const char* file = strrchr(path, '\\');
		CrashAppend(t, file ? file + 1 : path);
		CrashAppend(t, " + ");
		CrashAppendHex(t, addr - (DWORD64)(ULONG_PTR)mbi.AllocationBase);
	} else {
		CrashAppendHex(t, addr);
	}
	char symBuf[sizeof(SYMBOL_INFO) + 256];
	SYMBOL_INFO* sym = (SYMBOL_INFO*)symBuf;
	memset(sym, 0, sizeof(SYMBOL_INFO));
	sym->SizeOfStruct = sizeof(SYMBOL_INFO);
	sym->MaxNameLen = 255;
	DWORD64 disp = 0;
	if (SymFromAddr(process, addr, &disp, sym)) {
		CrashAppend(t, "  (");
		CrashAppend(t, sym->Name);
		CrashAppend(t, " + ");
		CrashAppendHex(t, disp);
		CrashAppend(t, ")");
	}
	CrashAppend(t, "\n");
}

static void CrashBacktrace(CrashText* t, EXCEPTION_POINTERS* ep) {
	HANDLE process = GetCurrentProcess();
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
	SymInitialize(process, NULL, TRUE);
#if defined(__x86_64__) || defined(_M_X64)
	// StackWalk64 scribbles on the context, so it gets a copy.
	CONTEXT ctx = *ep->ContextRecord;
	STACKFRAME64 frame = {};
	frame.AddrPC.Offset = ctx.Rip;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = ctx.Rbp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = ctx.Rsp;
	frame.AddrStack.Mode = AddrModeFlat;
	for (int i = 0; i < 64; i++) {
		if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &ctx, NULL,
		                 SymFunctionTableAccess64, SymGetModuleBase64, NULL) || !frame.AddrPC.Offset)
			break;
		CrashFrame(t, process, i, frame.AddrPC.Offset);
	}
#else
	// Our own stack, so the filter's frames come first.
	CrashFrame(t, process, 0, (DWORD64)(ULONG_PTR)ep->ExceptionRecord->ExceptionAddress);
	void* frames[62];
	USHORT n = RtlCaptureStackBackTrace(0, 62, frames, NULL);
	for (USHORT i = 0; i < n; i++) CrashFrame(t, process, i + 1, (DWORD64)(ULONG_PTR)frames[i]);
#endif
}

static bool CrashWriteFile(const char* path, const char* data, size_t len) {
	HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD wrote = 0;
	BOOL ok = WriteFile(h, data, (DWORD)len, &wrote, NULL);
	CloseHandle(h);
	return ok && wrote == len;
}

static void CrashMinidump(const char* path, EXCEPTION_POINTERS* ep) {
	HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	MINIDUMP_EXCEPTION_INFORMATION mei;
	mei.ThreadId = GetCurrentThreadId();
	mei.ExceptionPointers = ep;
	mei.ClientPointers = FALSE;
	BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h,
	                            (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithThreadInfo), &mei, NULL, NULL);
	CloseHandle(h);
	if (!ok) DeleteFileA(path);
}

static void CrashLogTail(CrashText* out) {
	CrashAppend(out, "\nLast lines of theseus.log:\n");
	HANDLE h = s_logPath[0] ? CreateFileA(s_logPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	                                      NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)
	                        : INVALID_HANDLE_VALUE;
	if (h == INVALID_HANDLE_VALUE) {
		CrashAppend(out, "(no log this run; it went to the console)\n");
		return;
	}
	LARGE_INTEGER size = {}, from = {};
	GetFileSizeEx(h, &size);
	from.QuadPart = size.QuadPart > CRASH_LOG_READ ? size.QuadPart - CRASH_LOG_READ : 0;
	DWORD got = 0;
	if (SetFilePointerEx(h, from, NULL, FILE_BEGIN))
		ReadFile(h, s_logTail, sizeof(s_logTail), &got, NULL);
	CloseHandle(h);
	size_t start = CrashTailStart(s_logTail, got, CRASH_LOG_LINES, from.QuadPart > 0);
	CrashRedact(out, s_logTail + start, got - start, s_crashHome, true);
}

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
	// One report. A crash inside it goes on to Windows; another thread's
	// crash waits for the end this one brings.
	static volatile LONG crashedThread = 0;
	LONG me = (LONG)GetCurrentThreadId();
	LONG first = InterlockedCompareExchange(&crashedThread, me, 0);
	if (first == me) return EXCEPTION_CONTINUE_SEARCH;
	if (first) Sleep(INFINITE);
	fflush(stdout);

	SYSTEMTIME st;
	GetLocalTime(&st);
	char stem[MAX_PATH + 64], path[MAX_PATH + 80];
	snprintf(stem, sizeof(stem), "%s\\%04u-%02u-%02u_%02u-%02u-%02u", s_crashDir, st.wYear, st.wMonth, st.wDay,
	         st.wHour, st.wMinute, st.wSecond);
	snprintf(path, sizeof(path), "%s.dmp", stem);
	CrashMinidump(path, ep);

	EXCEPTION_RECORD* er = ep->ExceptionRecord;
	CrashText* r = &s_report;
	r->len = 0;
	char line[256];
	CrashAppend(r, "Theseus crash report\n");
	CrashAppend(r, s_crashHeader);
	snprintf(line, sizeof(line), "Time: %04u-%02u-%02u %02u:%02u:%02u\nCrash: %s (0x%08lX)", st.wYear, st.wMonth,
	         st.wDay, st.wHour, st.wMinute, st.wSecond, ExceptionName(er->ExceptionCode), er->ExceptionCode);
	CrashAppend(r, line);
	if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
		snprintf(line, sizeof(line), ", %s address 0x%llx", er->ExceptionInformation[0] == 1 ? "writing" :
		         er->ExceptionInformation[0] == 8 ? "executing" : "reading",
		         (unsigned long long)er->ExceptionInformation[1]);
		CrashAppend(r, line);
	}
	CrashAppend(r, "\n\nBacktrace:\n");
	CrashBacktrace(r, ep);

	CrashText* out = &s_redacted;
	out->len = 0;
	CrashRedact(out, r->data, r->len, s_crashHome, true);
	size_t summaryLen = out->len;
	CrashLogTail(out);

	snprintf(path, sizeof(path), "%s.txt", stem);
	bool saved = CrashWriteFile(path, out->data, out->len);

	fprintf(stderr, "\n[CRASH] %.*s", (int)summaryLen, out->data);
	if (saved) {
		r->len = 0;
		CrashRedact(r, path, strlen(path), s_crashHome, true);
		fprintf(stderr, "[CRASH] Report saved to %s\n", r->data);
	}
	fflush(stderr);
	return EXCEPTION_EXECUTE_HANDLER;
}

static void CrashAbort(int) {
	RaiseException(CRASH_ABORT_EXCEPTION, EXCEPTION_NONCONTINUABLE, 0, NULL);
}

// MrBruh's: RtlGetVersion tells the truth where GetVersionEx won't, and
// Wine owns up through ntdll.
static void CrashOsName(char* out, size_t outSize) {
	typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW*);
	typedef const char* (CDECL *WineGetVersionFn)(void);
	HMODULE ntdll = GetModuleHandleA("ntdll.dll");
	RtlGetVersionFn rtlGetVersion = ntdll ? (RtlGetVersionFn)(void*)GetProcAddress(ntdll, "RtlGetVersion") : NULL;
	WineGetVersionFn wineGetVersion = ntdll ? (WineGetVersionFn)(void*)GetProcAddress(ntdll, "wine_get_version") : NULL;
	OSVERSIONINFOW v = {};
	v.dwOSVersionInfoSize = sizeof(v);
	if (!rtlGetVersion || rtlGetVersion(&v) != 0) v.dwMajorVersion = v.dwMinorVersion = v.dwBuildNumber = 0;
	int n = snprintf(out, outSize, "Windows %lu.%lu.%lu", (unsigned long)v.dwMajorVersion,
	                 (unsigned long)v.dwMinorVersion, (unsigned long)v.dwBuildNumber);
	if (wineGetVersion && n > 0 && (size_t)n < outSize)
		snprintf(out + n, outSize - (size_t)n, " (Wine %.32s)", wineGetVersion());
}

// Shipped Configs/version: "[default]" then "version=0.4.0".
static void CrashVersion(const char* shippedDir, char* out, size_t outSize) {
	snprintf(out, outSize, "unknown");
	char path[MAX_PATH], line[128];
	snprintf(path, sizeof(path), "%s\\Configs\\version", shippedDir);
	FILE* f = fopen(path, "r");
	if (!f) return;
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "version=", 8) != 0) continue;
		line[strcspn(line, "\r\n")] = '\0';
		snprintf(out, outSize, "%s", line + 8);
		break;
	}
	fclose(f);
}

// Keeps the newest CRASH_KEEP reports and their dumps. The names sort by time.
static void TrimCrashReports(const char* ext) {
	char pattern[MAX_PATH + 16];
	snprintf(pattern, sizeof(pattern), "%s\\*%s", s_crashDir, ext);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	std::vector<std::string> names;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) names.push_back(fd.cFileName);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	if (names.size() <= CRASH_KEEP) return;
	std::sort(names.begin(), names.end());
	for (size_t i = 0; i + CRASH_KEEP < names.size(); i++)
		DeleteFileA((std::string(s_crashDir) + "\\" + names[i]).c_str());
}

void Host_InstallCrashHandler(const char* userDataDir, const char* shippedDir) {
	snprintf(s_crashDir, sizeof(s_crashDir), "%s\\crashes", userDataDir);
	CreateDirectoryA(s_crashDir, NULL);
	const char* home = getenv("USERPROFILE");
	snprintf(s_crashHome, sizeof(s_crashHome), "%s", home ? home : "");

	char version[64], os[128];
	CrashVersion(shippedDir, version, sizeof(version));
	CrashOsName(os, sizeof(os));
	snprintf(s_crashHeader, sizeof(s_crashHeader), "Version: %s\nOS: %s\n", version, os);

	SetUnhandledExceptionFilter(CrashFilter);
	ULONG guarantee = 64 * 1024;
	SetThreadStackGuarantee(&guarantee);
	signal(SIGABRT, CrashAbort);

	TrimCrashReports(".txt");
	TrimCrashReports(".dmp");
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

bool Host_ExePath(char* out, size_t outSize) {
	DWORD len = GetModuleFileNameA(NULL, out, (DWORD)outSize);
	return len > 0 && len < outSize;
}

void Host_OpenFolder(const char* dir) {
	ShellExecuteA(NULL, "open", dir, NULL, NULL, SW_SHOWNORMAL);
}

bool Host_RegistryString(const char* subKey, const char* value, char* out, size_t outSize) {
	HKEY hKey;
	if (RegOpenKeyExA(HKEY_CURRENT_USER, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
	DWORD size = (DWORD)outSize;
	DWORD type = 0;
	bool ok = RegQueryValueExA(hKey, value, NULL, &type, (BYTE*)out, &size) == ERROR_SUCCESS &&
	          (type == REG_SZ || type == REG_EXPAND_SZ);
	RegCloseKey(hKey);
	if (ok) out[outSize - 1] = '\0';
	return ok;
}

static HANDLE OpenCdromDevice() {
	char path[] = "\\\\.\\D:";
	for (char c = 'D'; c <= 'H'; ++c) {
		char root[4] = { c, ':', '\\', 0 };
		if (GetDriveTypeA(root) != DRIVE_CDROM) continue;
		path[4] = c;
		HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		                       nullptr, OPEN_EXISTING, 0, nullptr);
		if (h != INVALID_HANDLE_VALUE) return h;
	}
	return INVALID_HANDLE_VALUE;
}

int Host_CdReadToc(int* durations, int maxTracks, bool* allAudio) {
	*allAudio = true;
	HANDLE h = OpenCdromDevice();
	if (h == INVALID_HANDLE_VALUE) return -1;

	CDROM_TOC toc;
	DWORD br = 0;
	BOOL ok = DeviceIoControl(h, IOCTL_CDROM_READ_TOC, nullptr, 0, &toc, sizeof(toc), &br, nullptr);
	CloseHandle(h);
	if (!ok) return -1;

	int count = toc.LastTrack - toc.FirstTrack + 1;
	for (int i = 0; i < count; ++i) {
		int ti = toc.FirstTrack - 1 + i;  // index into TrackData
		if (toc.TrackData[ti].Control & 4) *allAudio = false;
		if (durations && i < maxTracks) {
			UCHAR* a = toc.TrackData[ti].Address;
			UCHAR* b = toc.TrackData[ti + 1].Address;
			int t0 = (int)a[1] * 60 + a[2];
			int t1 = (int)b[1] * 60 + b[2];
			durations[i] = (t1 > t0) ? (t1 - t0) : 0;
		}
	}
	return count;
}

const char* Host_Name() { return "Windows"; }
const char* Host_DefaultShaderDir() { return "dx11"; }

void Host_UserDataDir(char* out, size_t outSize) {
	const char* appdata = getenv("APPDATA");
	if (appdata && *appdata) {
		snprintf(out, outSize, "%s\\Theseus", appdata);
		return;
	}
	const char* home = getenv("HOME");
	if (!home || !*home) home = getenv("USERPROFILE");
	snprintf(out, outSize, "%s\\AppData\\Roaming\\Theseus", home ? home : "");
}

void Host_ShippedDir(const char* exeDir, char* out, size_t outSize) {
	snprintf(out, outSize, "%s", exeDir);
}
