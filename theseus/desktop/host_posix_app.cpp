// host_posix_app.cpp: the process half of host.h for macOS and Linux.

#include "host.h"

#include <SDL.h>
#include <SDL_syswm.h>

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(__APPLE__) || defined(__GLIBC__)
#include <execinfo.h>
#define HAS_BACKTRACE 1
#endif

void Host_ConsoleOrLog(const char*) {}
void Host_BoostProcess() {}

static void CrashHandler(int sig) {
	fprintf(stderr, "\n[CRASH] Signal %d (%s)\n", sig, sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT" : "OTHER");
	fflush(stderr);
#ifdef HAS_BACKTRACE
	void* bt[64];
	int n = backtrace(bt, 64);
	backtrace_symbols_fd(bt, n, 2);
#endif
	_exit(128 + sig);
}

void Host_InstallCrashHandler() {
	signal(SIGSEGV, CrashHandler);
	signal(SIGBUS, CrashHandler);
	signal(SIGABRT, CrashHandler);
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
#ifdef __APPLE__
	*nwh = wmi.info.cocoa.window;
#else
	// Branch on SDL's actual subsystem. Feeding x11 lanes to bgfx on a
	// Wayland session hands RADV a NULL Display and it crashes in
	// vkGetPhysicalDeviceSurfaceSupportKHR.
	if (wmi.subsystem == SDL_SYSWM_WAYLAND) {
		*ndt = wmi.info.wl.display;
		*nwh = wmi.info.wl.surface;
		*wayland = true;
	} else {
		*ndt = wmi.info.x11.display;
		*nwh = (void*)(uintptr_t)wmi.info.x11.window;
	}
#endif
	return true;
}

// Put back the LD_LIBRARY_PATH AppRun stashed, so a spawned /bin/sh doesn't
// load the AppImage's bundled readline. No-op outside an AppImage.
static void RestoreHostLibraryPath() {
	const char* host = getenv("THESEUS_HOST_LD_LIBRARY_PATH");
	if (!host) return;
	if (*host) setenv("LD_LIBRARY_PATH", host, 1);
	else       unsetenv("LD_LIBRARY_PATH");
}

// Runs in the forked child and never returns on success.
static void ExecLaunch(const char* spec, bool isUrl, bool isSteamUrl) {
	if (isSteamUrl) {
#ifdef __APPLE__
		// macOS: system handler works fine for steam://
		execlp("open", "open", spec, (char*)NULL);
#else
		// Linux: bypass xdg-open/KIO; steam binary takes the URL directly.
		execlp("steam", "steam", spec, (char*)NULL);
#endif
	} else if (isUrl) {
#ifdef __APPLE__
		execlp("open", "open", spec, (char*)NULL);
#else
		execlp("xdg-open", "xdg-open", spec, (char*)NULL);
#endif
	} else {
		// Raw command line: shell parses paths, args, scripts, etc.
		execl("/bin/sh", "sh", "-c", spec, (char*)NULL);
	}
}

bool Host_Spawn(const char* spec, bool isUrl, bool isSteamUrl, const char* workdir,
                char* err, size_t errSize) {
	pid_t pid = fork();
	if (pid == 0) {
		if (workdir) (void)chdir(workdir);
		RestoreHostLibraryPath();
		ExecLaunch(spec, isUrl, isSteamUrl);
		_exit(127);
	}
	if (pid < 0) {
		snprintf(err, errSize, "fork() failed: %s", strerror(errno));
		return false;
	}
	// Parent: don't wait. Fire-and-forget.
	return true;
}

bool Host_PickFile(const char* title, char* out, size_t outSize) {
	char cmd[512];
#if defined(__APPLE__)
	snprintf(cmd, sizeof(cmd),
	         "osascript -e 'POSIX path of (choose file with prompt \"%s\")' 2>/dev/null", title);
#else
	snprintf(cmd, sizeof(cmd), "zenity --file-selection --title=\"%s\" 2>/dev/null", title);
#endif
	FILE* p = popen(cmd, "r");
	if (!p) return false;
	size_t n = 0;
	char buf[2048];
	out[0] = '\0';
	while (fgets(buf, sizeof(buf), p)) {
		size_t len = strlen(buf);
		if (n + len >= outSize) len = outSize - 1 - n;
		memcpy(out + n, buf, len);
		n += len;
		out[n] = '\0';
	}
	pclose(p);
	while (n && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = '\0';
	return n > 0;
}
