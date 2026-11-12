// host_posix_app.cpp: the process half of host.h for macOS and Linux.

#include "host.h"

#include <SDL.h>
#include <SDL_syswm.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#if defined(__APPLE__) || defined(__GLIBC__)
#include <execinfo.h>
#define HAS_BACKTRACE 1
#endif

// Everything still reaches the terminal, and a copy lands in theseus.log, so a
// Finder or desktop-entry launch leaves something a user can send. The last
// run's log is kept as theseus.prev.log.
static int s_termFd = -1;
static int s_logFd = -1;
static pthread_t s_teeThread;
static bool s_teeRunning = false;

static void* TeeThread(void* arg) {
	int in = (int)(intptr_t)arg;
	char buf[4096];
	ssize_t n;
	while ((n = read(in, buf, sizeof(buf))) > 0) {
		if (s_termFd >= 0) (void)!write(s_termFd, buf, (size_t)n);
		if (s_logFd >= 0)  (void)!write(s_logFd, buf, (size_t)n);
	}
	close(in);
	return NULL;
}

// Closing the only write ends lets the thread drain the pipe and finish.
static void StopTee() {
	if (!s_teeRunning) return;
	fflush(stdout);
	fflush(stderr);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	pthread_join(s_teeThread, NULL);
	s_teeRunning = false;
}

void Host_ConsoleOrLog(const char* logPath) {
	// First launch: the user data folder doesn't exist yet.
	std::string dir = logPath;
	dir = dir.substr(0, dir.rfind('/'));
	for (size_t i = 1; i <= dir.size(); i++)
		if (i == dir.size() || dir[i] == '/')
			mkdir(dir.substr(0, i).c_str(), 0755);

	std::string prev = logPath;
	size_t dot = prev.rfind(".log");
	prev = (dot == std::string::npos) ? prev + ".prev" : prev.substr(0, dot) + ".prev.log";
	rename(logPath, prev.c_str());

	s_logFd = open(logPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (s_logFd < 0) return;
	int p[2];
	if (pipe(p) != 0) return;

	fflush(stdout);
	fflush(stderr);
	s_termFd = dup(STDERR_FILENO);
	dup2(p[1], STDOUT_FILENO);
	dup2(p[1], STDERR_FILENO);
	close(p[1]);
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	if (pthread_create(&s_teeThread, NULL, TeeThread, (void*)(intptr_t)p[0]) == 0) {
		s_teeRunning = true;
		atexit(StopTee);
	}
}
void Host_BoostProcess() {}

// Writes straight to the terminal and the log: the tee thread may never get
// to run again once we're in here.
static void CrashHandler(int sig) {
	char line[96];
	int len = snprintf(line, sizeof(line), "\n[CRASH] Signal %d (%s)\n", sig,
	                   sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT" : sig == SIGBUS ? "SIGBUS" : "OTHER");
	int fds[2] = { s_termFd >= 0 ? s_termFd : STDERR_FILENO, s_logFd };
#ifdef HAS_BACKTRACE
	void* bt[64];
	int n = backtrace(bt, 64);
#endif
	for (int i = 0; i < 2; i++) {
		if (fds[i] < 0) continue;
		(void)!write(fds[i], line, (size_t)len);
#ifdef HAS_BACKTRACE
		backtrace_symbols_fd(bt, n, fds[i]);
#endif
	}
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
		// Flatpak Steam has no steam on PATH. Stack buffers only, we're the
		// child of a threaded fork.
		const char* home = getenv("HOME");
		char userApp[512] = "";
		if (home) {
			strncpy(userApp, home, sizeof(userApp) - 1);
			strncat(userApp, "/.local/share/flatpak/app/com.valvesoftware.Steam",
			        sizeof(userApp) - strlen(userApp) - 1);
		}
		struct stat st;
		if (stat("/var/lib/flatpak/app/com.valvesoftware.Steam", &st) == 0 ||
		    (userApp[0] && stat(userApp, &st) == 0))
			execlp("flatpak", "flatpak", "run", "com.valvesoftware.Steam", spec, (char*)NULL);
		execlp("xdg-open", "xdg-open", spec, (char*)NULL);
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

bool Host_ExePath(char* out, size_t outSize) {
#ifdef __APPLE__
	uint32_t sz = (uint32_t)outSize;
	return _NSGetExecutablePath(out, &sz) == 0;
#else
	ssize_t len = readlink("/proc/self/exe", out, outSize - 1);
	if (len <= 0) return false;
	out[len] = '\0';
	return true;
#endif
}

void Host_OpenFolder(const char* dir) {
	// Fork so a slow file manager can't stall us, and no shell is involved.
	pid_t pid = fork();
	if (pid == 0) {
#ifdef __APPLE__
		execlp("open", "open", dir, (char*)NULL);
#else
		// Under flatpak we want the host's file manager.
		if (getenv("FLATPAK_ID"))
			execlp("flatpak-spawn", "flatpak-spawn", "--host", "xdg-open", dir, (char*)NULL);
		execlp("xdg-open", "xdg-open", dir, (char*)NULL);
#endif
		_exit(127);
	}
}

bool Host_RegistryString(const char*, const char*, char*, size_t) { return false; }

// Linux reads its TOC in cdaudio.cpp.
int Host_CdReadToc(int*, int, bool*) { return -1; }

const char* Host_Name() {
#ifdef __APPLE__
	return "macOS";
#else
	return "Linux";
#endif
}

const char* Host_DefaultShaderDir() {
#ifdef __APPLE__
	return "metal";
#else
	return "spirv";
#endif
}

void Host_UserDataDir(char* out, size_t outSize) {
	const char* home = getenv("HOME");
	if (!home) home = "";
#ifdef __APPLE__
	snprintf(out, outSize, "%s/Library/Application Support/Theseus", home);
#else
	const char* xdg = getenv("XDG_DATA_HOME");
	if (xdg && *xdg)
		snprintf(out, outSize, "%s/theseus", xdg);
	else
		snprintf(out, outSize, "%s/.local/share/theseus", home);
#endif
}

void Host_ShippedDir(const char* exeDir, char* out, size_t outSize) {
#ifdef __APPLE__
	// Inside a .app the binary sits in Contents/MacOS and the payload in
	// Contents/Resources. Loose builds keep everything beside the binary.
	size_t n = strlen(exeDir);
	const char kTail[] = "/Contents/MacOS";
	size_t t = sizeof(kTail) - 1;
	if (n > t && strcmp(exeDir + n - t, kTail) == 0) {
		snprintf(out, outSize, "%.*s/Contents/Resources", (int)(n - t), exeDir);
		return;
	}
#endif
	snprintf(out, outSize, "%s", exeDir);
}
