// host_posix_app.cpp: the process half of host.h for macOS and Linux.

#include "host.h"

#include <SDL.h>
#include <SDL_syswm.h>

#include "host_crash.h"

#include <algorithm>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <sys/sysctl.h>
#include <sys/ucontext.h>
#else
#include <ucontext.h>
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
static char s_logPath[1024];
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

// An exec restart keeps our fds but not the tee thread, so the new process
// would write into a pipe nobody reads. This names the real terminal and the
// old pipe's read end so it can put stdout/stderr back first.
#define TEE_ENV "THESEUS_TEE_FDS"

static bool FdOpen(int fd) { return fd > STDERR_FILENO && fcntl(fd, F_GETFD) != -1; }

static bool UndoInheritedTee() {
	const char* env = getenv(TEE_ENV);
	if (!env) return false;
	int term = -1, oldRead = -1;
	struct stat st;
	bool ok = sscanf(env, "%d,%d", &term, &oldRead) == 2 && FdOpen(term) && FdOpen(oldRead) &&
	          fstat(oldRead, &st) == 0 && S_ISFIFO(st.st_mode);
	if (ok) {
		dup2(term, STDOUT_FILENO);
		dup2(term, STDERR_FILENO);
		close(term);
		close(oldRead);
	}
	unsetenv(TEE_ENV);
	return ok;
}

void Host_ConsoleOrLog(const char* logPath) {
	bool restarted = UndoInheritedTee();

	// First launch: the user data folder doesn't exist yet.
	std::string dir = logPath;
	dir = dir.substr(0, dir.rfind('/'));
	for (size_t i = 1; i <= dir.size(); i++)
		if (i == dir.size() || dir[i] == '/')
			mkdir(dir.substr(0, i).c_str(), 0755);

	// A restart carries on in the same log unless it's grown big.
	struct stat st;
	bool append = restarted && stat(logPath, &st) == 0 && st.st_size < 4 * 1024 * 1024;
	if (!append) {
		std::string prev = logPath;
		size_t dot = prev.rfind(".log");
		prev = (dot == std::string::npos) ? prev + ".prev" : prev.substr(0, dot) + ".prev.log";
		rename(logPath, prev.c_str());
	}

	s_logFd = open(logPath, O_WRONLY | O_CREAT | O_CLOEXEC | (append ? O_APPEND : O_TRUNC), 0644);
	if (s_logFd < 0) return;
	snprintf(s_logPath, sizeof(s_logPath), "%s", logPath);
	int p[2];
	if (pipe(p) != 0) return;

	fflush(stdout);
	fflush(stderr);
	s_termFd = dup(STDERR_FILENO);
	dup2(p[1], STDOUT_FILENO);
	dup2(p[1], STDERR_FILENO);
	close(p[1]);
	// The read end stays open across exec so the new process's first writes
	// land in the pipe instead of raising SIGPIPE.
	char fds[32];
	snprintf(fds, sizeof(fds), "%d,%d", s_termFd, p[0]);
	setenv(TEE_ENV, fds, 1);
	if (append) {
		static const char kMark[] = "[Host] Restarted\n";
		(void)!write(s_logFd, kMark, sizeof(kMark) - 1);
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	if (pthread_create(&s_teeThread, NULL, TeeThread, (void*)(intptr_t)p[0]) == 0) {
		s_teeRunning = true;
		atexit(StopTee);
	}
}
void Host_BoostProcess() {}

// Crash reports. Everything the handler needs is worked out at install time;
// inside it we only fill static buffers and write(). Each crash leaves
// crashes/<local time>.txt in the user data folder: version, OS, signal,
// backtrace and the end of theseus.log, with the home folder shown as ~.
static const int kCrashSignals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
static char s_crashDir[1024];
static char s_crashHome[1024];
static char s_crashHeader[1024];
static long s_crashUtcOffset;
static int s_probe[2] = { -1, -1 };
static char s_altStack[64 * 1024];
static CrashText s_report, s_redacted, s_crashPath;
static char s_logTail[CRASH_LOG_READ];

static const char* SignalName(int sig) {
	switch (sig) {
	case SIGSEGV: return "SIGSEGV";
	case SIGBUS:  return "SIGBUS";
	case SIGILL:  return "SIGILL";
	case SIGFPE:  return "SIGFPE";
	case SIGABRT: return "SIGABRT";
	default:      return "signal";
	}
}

static void CrashFrame(CrashText* t, int idx, uintptr_t addr) {
	CrashAppend(t, "  #");
	CrashAppendDec(t, (uint64_t)idx, 2);
	CrashAppend(t, "  ");
	Dl_info di;
	if (dladdr((void*)addr, &di) && di.dli_fname && di.dli_fbase) {
		const char* file = strrchr(di.dli_fname, '/');
		CrashAppend(t, file ? file + 1 : di.dli_fname);
		CrashAppend(t, " + ");
		CrashAppendHex(t, addr - (uintptr_t)di.dli_fbase);
		if (di.dli_sname && di.dli_saddr) {
			CrashAppend(t, "  (");
			CrashAppend(t, di.dli_sname);
			CrashAppend(t, " + ");
			CrashAppendHex(t, addr - (uintptr_t)di.dli_saddr);
			CrashAppend(t, ")");
		}
	} else {
		CrashAppendHex(t, addr);
	}
	CrashAppend(t, "\n");
}

static void ContextRegisters(void* context, uintptr_t* pc, uintptr_t* fp) {
	ucontext_t* uc = (ucontext_t*)context;
#if defined(__APPLE__) && defined(__aarch64__)
	*pc = (uintptr_t)uc->uc_mcontext->__ss.__pc;
	*fp = (uintptr_t)uc->uc_mcontext->__ss.__fp;
#elif defined(__APPLE__) && defined(__x86_64__)
	*pc = (uintptr_t)uc->uc_mcontext->__ss.__rip;
	*fp = (uintptr_t)uc->uc_mcontext->__ss.__rbp;
#elif defined(__linux__) && defined(__aarch64__)
	*pc = (uintptr_t)uc->uc_mcontext.pc;
	*fp = (uintptr_t)uc->uc_mcontext.regs[29];
#elif defined(__linux__) && defined(__x86_64__)
	*pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
	*fp = (uintptr_t)uc->uc_mcontext.gregs[REG_RBP];
#else
	(void)uc;
	*pc = 0;
	*fp = 0;
#endif
}

#ifdef __GLIBC__
// glibc unwinds through the signal frame without frame pointers. Our own
// frames come first, so start at the one that crashed.
static void CrashBacktrace(CrashText* t, void* context) {
	uintptr_t pc, fp;
	ContextRegisters(context, &pc, &fp);
	void* bt[64];
	int n = backtrace(bt, 64), first = -1, idx = 0;
	for (int i = 0; i < n && first < 0; i++)
		if ((uintptr_t)bt[i] == pc) first = i;
	if (first < 0) {
		if (pc) CrashFrame(t, idx++, pc);
		first = 1;
	}
	for (int i = first; i < n; i++) CrashFrame(t, idx++, (uintptr_t)bt[i]);
}
#else
// A pipe takes the bytes or fails with EFAULT, so this never faults itself.
static bool Readable(const void* p, size_t n) {
	char drain[64];
	ssize_t w = write(s_probe[1], p, n);
	if (w > 0)
		while (read(s_probe[0], drain, sizeof(drain)) > 0) {}
	return w == (ssize_t)n;
}

// The frame pointer chain from the crashed context, each record checked
// readable first. arm64 macOS always keeps frame pointers.
static void CrashBacktrace(CrashText* t, void* context) {
	uintptr_t pc, fp;
	ContextRegisters(context, &pc, &fp);
	int idx = 0;
	if (pc) CrashFrame(t, idx++, pc);
	while (idx < 64 && fp && !(fp & (sizeof(uintptr_t) - 1)) && Readable((void*)fp, 2 * sizeof(uintptr_t))) {
		const uintptr_t* rec = (const uintptr_t*)fp;
		if (!rec[1]) break;
		CrashFrame(t, idx++, rec[1]);
		if (rec[0] <= fp) break;
		fp = rec[0];
	}
}
#endif

static void WriteAll(int fd, const char* p, size_t n) {
	while (n) {
		ssize_t w = write(fd, p, n);
		if (w <= 0) return;
		p += w;
		n -= (size_t)w;
	}
}

static void WriteCrashReport(int sig, siginfo_t* info, void* context) {
	CrashText* r = &s_report;
	r->len = 0;
	time_t now = time(NULL);
	CrashAppend(r, "Theseus crash report\n");
	CrashAppend(r, s_crashHeader);
	CrashAppend(r, "Time: ");
	CrashAppendTime(r, (int64_t)now + s_crashUtcOffset, false);
	CrashAppend(r, "\nCrash: ");
	CrashAppend(r, SignalName(sig));
	CrashAppend(r, " (signal ");
	CrashAppendDec(r, (uint64_t)sig);
	CrashAppend(r, ")");
	if (info && sig != SIGABRT) {
		CrashAppend(r, " at address ");
		CrashAppendHex(r, (uintptr_t)info->si_addr);
	}
	CrashAppend(r, "\n\nBacktrace:\n");
	CrashBacktrace(r, context);

	CrashText* out = &s_redacted;
	out->len = 0;
	CrashRedact(out, r->data, r->len, s_crashHome, false);
	size_t summaryLen = out->len;

	// Read it before our own lines land in the log.
	CrashAppend(out, "\nLast lines of theseus.log:\n");
	int lfd = s_logPath[0] ? open(s_logPath, O_RDONLY) : -1;
	if (lfd >= 0) {
		off_t size = lseek(lfd, 0, SEEK_END);
		off_t from = size > (off_t)CRASH_LOG_READ ? size - (off_t)CRASH_LOG_READ : 0;
		size_t got = 0;
		if (size > 0 && lseek(lfd, from, SEEK_SET) == from) {
			ssize_t n;
			while (got < sizeof(s_logTail) && (n = read(lfd, s_logTail + got, sizeof(s_logTail) - got)) > 0)
				got += (size_t)n;
		}
		close(lfd);
		size_t start = CrashTailStart(s_logTail, got, CRASH_LOG_LINES, from > 0);
		CrashRedact(out, s_logTail + start, got - start, s_crashHome, false);
	} else {
		CrashAppend(out, "(no log this run)\n");
	}

	CrashText* path = &s_crashPath;
	path->len = 0;
	CrashAppend(path, s_crashDir);
	CrashAppend(path, "/");
	CrashAppendTime(path, (int64_t)now + s_crashUtcOffset, true);
	size_t stem = path->len;
	int fd = -1;
	for (int tries = 0; fd < 0 && tries < 10; tries++) {
		path->len = stem;
		if (tries) { CrashAppend(path, "-"); CrashAppendDec(path, (uint64_t)tries); }
		CrashAppend(path, ".txt");
		fd = open(path->data, O_WRONLY | O_CREAT | O_EXCL, 0644);
		if (fd < 0 && errno != EEXIST) break;
	}
	if (fd >= 0) {
		WriteAll(fd, out->data, out->len);
		close(fd);
	}

	// The tee thread may never run again, so the terminal and the log get
	// the summary straight from here.
	r->len = 0;
	CrashRedact(r, path->data, path->len, s_crashHome, false);
	int fds[2] = { s_termFd >= 0 ? s_termFd : STDERR_FILENO, s_logFd };
	for (int i = 0; i < 2; i++) {
		if (fds[i] < 0) continue;
		WriteAll(fds[i], "\n[CRASH] ", 9);
		WriteAll(fds[i], out->data, summaryLen);
		if (fd >= 0) {
			WriteAll(fds[i], "[CRASH] Report saved to ", 24);
			WriteAll(fds[i], r->data, r->len);
			WriteAll(fds[i], "\n", 1);
		}
	}
}

static void CrashHandler(int sig, siginfo_t* info, void* context) {
	static volatile sig_atomic_t entered;
	if (!entered) {
		entered = 1;
		alarm(10);   // a hung report still ends the process
		WriteCrashReport(sig, info, context);
	}
	// Back to the default action so the OS sees the real crash.
	signal(sig, SIG_DFL);
	raise(sig);
}

static void CrashOsName(char* out, size_t outSize) {
	struct utsname u;
	bool haveUname = uname(&u) == 0;
#ifdef __APPLE__
	char ver[64];
	size_t len = sizeof(ver);
	if (sysctlbyname("kern.osproductversion", ver, &len, NULL, 0) == 0)
		snprintf(out, outSize, "macOS %s (%s)", ver, haveUname ? u.machine : "?");
	else
		snprintf(out, outSize, "macOS, Darwin %s", haveUname ? u.release : "?");
#else
	char line[256], pretty[128] = "Linux";
	if (FILE* f = fopen("/etc/os-release", "r")) {
		while (fgets(line, sizeof(line), f)) {
			if (strncmp(line, "PRETTY_NAME=", 12) != 0) continue;
			char* v = line + 12 + (line[12] == '"');
			v[strcspn(v, "\"\n")] = '\0';
			snprintf(pretty, sizeof(pretty), "%s", v);
		}
		fclose(f);
	}
	if (haveUname)
		snprintf(out, outSize, "%s, kernel %s (%s)", pretty, u.release, u.machine);
	else
		snprintf(out, outSize, "%s", pretty);
#endif
}

// Shipped Configs/version: "[default]" then "version=0.4.0".
static void CrashVersion(const char* shippedDir, char* out, size_t outSize) {
	snprintf(out, outSize, "unknown");
	char path[1024], line[128];
	snprintf(path, sizeof(path), "%s/Configs/version", shippedDir);
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

// Keeps the newest CRASH_KEEP reports. The names sort by time.
static void TrimCrashReports() {
	DIR* d = opendir(s_crashDir);
	if (!d) return;
	std::vector<std::string> names;
	while (struct dirent* e = readdir(d)) {
		size_t n = strlen(e->d_name);
		if (n > 4 && strcmp(e->d_name + n - 4, ".txt") == 0) names.push_back(e->d_name);
	}
	closedir(d);
	if (names.size() <= CRASH_KEEP) return;
	std::sort(names.begin(), names.end());
	for (size_t i = 0; i + CRASH_KEEP < names.size(); i++)
		unlink((std::string(s_crashDir) + "/" + names[i]).c_str());
}

void Host_InstallCrashHandler(const char* userDataDir, const char* shippedDir) {
	snprintf(s_crashDir, sizeof(s_crashDir), "%s/crashes", userDataDir);
	mkdir(s_crashDir, 0755);
	const char* home = getenv("HOME");
	snprintf(s_crashHome, sizeof(s_crashHome), "%s", home ? home : "");

	char version[64], os[256];
	CrashVersion(shippedDir, version, sizeof(version));
	CrashOsName(os, sizeof(os));
	snprintf(s_crashHeader, sizeof(s_crashHeader), "Version: %s\nOS: %s\n", version, os);
	fprintf(stdout, "[Host] UIX Desktop %s on %s\n", version, os);

	time_t now = time(NULL);
	struct tm lt;
	if (localtime_r(&now, &lt)) s_crashUtcOffset = lt.tm_gmtoff;

	if (pipe(s_probe) == 0)
		for (int i = 0; i < 2; i++) {
			fcntl(s_probe[i], F_SETFL, fcntl(s_probe[i], F_GETFL) | O_NONBLOCK);
			fcntl(s_probe[i], F_SETFD, FD_CLOEXEC);
		}

	// Room to run after a stack overflow.
	stack_t ss = {};
	ss.ss_sp = s_altStack;
	ss.ss_size = sizeof(s_altStack);
	sigaltstack(&ss, NULL);

	// First calls can take loader locks a crash might be holding.
	Dl_info di;
	dladdr((void*)Host_InstallCrashHandler, &di);
#ifdef HAS_BACKTRACE
	void* bt[4];
	backtrace(bt, 4);
#endif

	struct sigaction sa = {};
	sa.sa_sigaction = CrashHandler;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset(&sa.sa_mask);
	for (int sig : kCrashSignals) sigaction(sig, &sa, NULL);

	TrimCrashReports();
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

bool Host_WindowOccluded(SDL_Window* window) {
#ifdef __APPLE__
	SDL_SysWMinfo wmi;
	SDL_VERSION(&wmi.version);
	if (!window || !SDL_GetWindowWMInfo(window, &wmi) || !wmi.info.cocoa.window) return false;
	// [NSWindow occlusionState], where bit 1 is NSWindowOcclusionStateVisible.
	unsigned long state = ((unsigned long (*)(id, SEL))objc_msgSend)(
		(id)wmi.info.cocoa.window, sel_registerName("occlusionState"));
	return (state & (1ul << 1)) == 0;
#else
	(void)window;
	return false;
#endif
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

struct HostProc { pid_t pid; bool done; };

bool Host_Spawn(const char* spec, bool isUrl, bool isSteamUrl, const char* workdir,
                char* err, size_t errSize, HostProc** proc) {
	if (proc) *proc = NULL;
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
	if (proc) {
		*proc = new HostProc;
		(*proc)->pid = pid;
		(*proc)->done = false;
	}
	return true;
}

bool Host_RunWait(const char* const* argv) {
	pid_t pid = fork();
	if (pid == 0) {
		RestoreHostLibraryPath();
		execvp(argv[0], (char* const*)argv);
		_exit(127);
	}
	if (pid < 0) return false;
	int status = 0;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) return false;
	}
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool Host_ProcDone(HostProc* proc) {
	if (!proc || proc->done) return true;
	int status;
	pid_t r = waitpid(proc->pid, &status, WNOHANG);
	if (r == proc->pid || (r < 0 && errno == ECHILD)) proc->done = true;
	return proc->done;
}

void Host_ProcFree(HostProc* proc) {
	delete proc;
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
bool Host_RegistryValue(int, const char*, const char*, char*, size_t) { return false; }
int Host_RegistrySubkeys(int, const char*, char (*)[256], int) { return 0; }
bool Host_RegistrySetString(const char*, const char*, const char*) { return false; }
bool Host_RegistryDelete(const char*, const char*) { return false; }

// Start at login. Both entries live under the user's home, so a run with a
// throwaway HOME never touches the real account.
static std::string LoginEntryPath() {
	const char* home = getenv("HOME");
	std::string h = home ? home : "";
#ifdef __APPLE__
	return h + "/Library/LaunchAgents/com.teamuix.theseus.plist";
#else
	const char* xdg = getenv("XDG_CONFIG_HOME");
	std::string base = (xdg && *xdg == '/') ? std::string(xdg) : h + "/.config";
	return base + "/autostart/uix-desktop.desktop";
#endif
}

static bool MakeDirs(const std::string& dir) {
	for (size_t i = 1; i <= dir.size(); i++)
		if (i == dir.size() || dir[i] == '/')
			if (mkdir(dir.substr(0, i).c_str(), 0755) != 0 && errno != EEXIST) return false;
	return true;
}

// Beside it first, then over it, so a half written entry never runs.
static bool WriteWhole(const std::string& path, const std::string& text) {
	if (!MakeDirs(path.substr(0, path.rfind('/')))) return false;
	std::string tmp = path + ".tmp";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f) return false;
	bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
	ok = (fclose(f) == 0) && ok;
	if (ok && rename(tmp.c_str(), path.c_str()) == 0) return true;
	unlink(tmp.c_str());
	return false;
}

#ifdef __APPLE__
static std::string XmlEscape(const std::string& in) {
	std::string out;
	for (char c : in) {
		if (c == '&') out += "&amp;";
		else if (c == '<') out += "&lt;";
		else if (c == '>') out += "&gt;";
		else out += c;
	}
	return out;
}

static std::string LoginEntryText(const std::string& exe) {
	return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	       "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
	       "<plist version=\"1.0\">\n<dict>\n"
	       "\t<key>Label</key>\n\t<string>com.teamuix.theseus</string>\n"
	       "\t<key>ProgramArguments</key>\n\t<array>\n"
	       "\t\t<string>" + XmlEscape(exe) + "</string>\n"
	       "\t\t<string>--console</string>\n"
	       "\t</array>\n"
	       "\t<key>RunAtLoad</key>\n\t<true/>\n"
	       "\t<key>LimitLoadToSessionType</key>\n\t<string>Aqua</string>\n"
	       "</dict>\n</plist>\n";
}
#else
// Exec= quoting from the desktop entry spec: the argument in double quotes
// with " ` $ \ backslashed, then every backslash doubled for the string
// itself, and % as %%.
static std::string ExecQuote(const std::string& in) {
	std::string q = "\"";
	for (char c : in) {
		if (c == '"' || c == '`' || c == '$' || c == '\\') q += '\\';
		q += c;
	}
	q += '"';
	std::string out;
	for (char c : q) {
		if (c == '\\') out += "\\\\";
		else if (c == '%') out += "%%";
		else out += c;
	}
	return out;
}

static std::string LoginEntryText(const std::string& exe) {
	return "[Desktop Entry]\n"
	       "Type=Application\n"
	       "Name=UIX Desktop\n"
	       "Comment=Starts UIX Desktop in Console Mode\n"
	       "Exec=" + ExecQuote(exe) + " --console\n"
	       "Terminal=false\n"
	       "X-GNOME-Autostart-enabled=true\n";
}
#endif

static bool PowerDryRun() {
	const char* v = getenv("UIX_DEV_POWER_DRYRUN");
	return v && *v && strcmp(v, "0") != 0;
}

bool Host_Power(int action) {
	static const char* const kNames[] = { "sleep", "restart", "shut down" };
	if (action < HOST_POWER_SLEEP || action > HOST_POWER_SHUTDOWN) return false;
#ifdef __APPLE__
	// System Events asks every app to quit first, the same as the Apple menu.
	const char* const kSleep[]    = { "pmset", "sleepnow", NULL };
	const char* const kRestart[]  = { "osascript", "-e", "tell application \"System Events\" to restart", NULL };
	const char* const kShutdown[] = { "osascript", "-e", "tell application \"System Events\" to shut down", NULL };
#else
	// logind lets the active local session do these without a password.
	const char* const kSleep[]    = { "systemctl", "suspend", NULL };
	const char* const kRestart[]  = { "systemctl", "reboot", NULL };
	const char* const kShutdown[] = { "systemctl", "poweroff", NULL };
#endif
	const char* const* argv = action == HOST_POWER_SLEEP ? kSleep : action == HOST_POWER_RESTART ? kRestart : kShutdown;
	std::string line;
	for (int i = 0; argv[i]; i++) {
		if (i) line += ' ';
		line += strchr(argv[i], ' ') ? "'" + std::string(argv[i]) + "'" : argv[i];
	}
	if (PowerDryRun()) {
		fprintf(stderr, "[Power] would %s: %s\n", kNames[action], line.c_str());
		return true;
	}
	fprintf(stderr, "[Power] %s: %s\n", kNames[action], line.c_str());
	fflush(stderr);
	pid_t pid = fork();
	if (pid == 0) {
		RestoreHostLibraryPath();
#ifndef __APPLE__
		if (getenv("FLATPAK_ID")) {
			const char* host[8] = { "flatpak-spawn", "--host" };
			for (int i = 0; argv[i] && i < 5; i++) host[i + 2] = argv[i];
			execvp(host[0], (char* const*)host);
		}
#endif
		execvp(argv[0], (char* const*)argv);
		_exit(127);
	}
	if (pid < 0) return false;
	// These answer quickly when they're refused. Still going after a few
	// seconds means it's happening.
	for (int i = 0; i < 30; i++) {
		int status;
		pid_t r = waitpid(pid, &status, WNOHANG);
		if (r == pid) {
			bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
			if (!ok) fprintf(stderr, "[Power] %s was refused\n", kNames[action]);
			return ok;
		}
		if (r < 0) return false;
		usleep(100 * 1000);
	}
	return true;
}

// The supervisor hands a stop on to the child and keeps waiting for it.
static volatile pid_t s_selfChild = 0;
static void ForwardStop(int sig) {
	if (s_selfChild > 0) kill(s_selfChild, sig);
}

int Host_RunSelf(const char* const* args, int* code, char* how, size_t howSize) {
	*code = 0;
	if (howSize) how[0] = '\0';
	char exe[1024];
	if (!Host_ExePath(exe, sizeof(exe))) return HOST_CHILD_FAILED;
	std::vector<const char*> argv;
	argv.push_back(exe);
	for (int i = 1; args[i]; i++) argv.push_back(args[i]);
	argv.push_back(NULL);

	struct sigaction sa = {}, old[3];
	sa.sa_handler = ForwardStop;
	sigemptyset(&sa.sa_mask);
	static const int kStops[3] = { SIGTERM, SIGINT, SIGHUP };
	for (int i = 0; i < 3; i++) sigaction(kStops[i], &sa, &old[i]);

	pid_t pid = fork();
	if (pid == 0) {
		for (int i = 0; i < 3; i++) sigaction(kStops[i], &old[i], NULL);
		execv(exe, (char* const*)argv.data());
		_exit(127);
	}
	int result = HOST_CHILD_FAILED;
	if (pid > 0) {
		s_selfChild = pid;
		int status = 0;
		pid_t r;
		while ((r = waitpid(pid, &status, 0)) < 0 && errno == EINTR) {}
		s_selfChild = 0;
		if (r == pid && WIFSIGNALED(status)) {
			*code = WTERMSIG(status);
			snprintf(how, howSize, "%s, signal %d", SignalName(*code), *code);
			result = HOST_CHILD_CRASHED;
		} else if (r == pid && WIFEXITED(status)) {
			*code = WEXITSTATUS(status);
			snprintf(how, howSize, "exit code %d", *code);
			result = HOST_CHILD_EXITED;
		}
	}
	for (int i = 0; i < 3; i++) sigaction(kStops[i], &old[i], NULL);
	return result;
}

bool Host_ExecKeepsProcess() { return true; }

#ifndef __APPLE__
static bool OnPath(const char* name) {
	const char* path = getenv("PATH");
	if (!path) return false;
	std::string dirs = path;
	size_t at = 0;
	while (at <= dirs.size()) {
		size_t end = dirs.find(':', at);
		if (end == std::string::npos) end = dirs.size();
		std::string full = dirs.substr(at, end - at) + "/" + name;
		if (end > at && access(full.c_str(), X_OK) == 0) return true;
		at = end + 1;
	}
	return false;
}

// What opens the settings: GNOME's, then Plasma's, then whatever claims
// settings:// links. Empty when none of them is here.
static const char* const* SettingsCommand() {
	static const char* const kGnome[] = { "gnome-control-center", NULL };
	static const char* const kPlasma[] = { "systemsettings", NULL };
	static const char* const kLink[] = { "xdg-open", "settings://", NULL };
	static int s_found = -1;
	if (s_found < 0) {
		s_found = 0;
		if (OnPath("gnome-control-center")) s_found = 1;
		else if (OnPath("systemsettings")) s_found = 2;
		else if (OnPath("xdg-open") && OnPath("xdg-mime")) {
			FILE* p = popen("xdg-mime query default x-scheme-handler/settings 2>/dev/null", "r");
			char line[256] = "";
			if (p) {
				if (!fgets(line, sizeof(line), p)) line[0] = '\0';
				pclose(p);
			}
			if (strstr(line, ".desktop")) s_found = 3;
		}
	}
	return s_found == 1 ? kGnome : s_found == 2 ? kPlasma : s_found == 3 ? kLink : NULL;
}
#endif

bool Host_HasSystemSettings() {
#ifdef __APPLE__
	return true;
#else
	return SettingsCommand() != NULL;
#endif
}

bool Host_OpenSystemSettings() {
#ifdef __APPLE__
	const char* const argv[] = { "open", "x-apple.systempreferences:", NULL };
#else
	const char* const* argv = SettingsCommand();
	if (!argv) return false;
#endif
	pid_t pid = fork();
	if (pid == 0) {
		RestoreHostLibraryPath();
		execvp(argv[0], (char* const*)argv);
		_exit(127);
	}
	return pid > 0;
}

bool Host_StartAtLogin() {
	struct stat st;
	return stat(LoginEntryPath().c_str(), &st) == 0;
}

bool Host_SetStartAtLogin(bool on) {
	std::string path = LoginEntryPath();
	if (!on) {
		if (unlink(path.c_str()) == 0 || errno == ENOENT) {
			fprintf(stderr, "[Console] login entry removed: %s\n", path.c_str());
			return true;
		}
		return false;
	}
	char exe[1024];
	if (!Host_ExePath(exe, sizeof(exe))) return false;
	std::string target = exe;
#ifndef __APPLE__
	// An AppImage runs from a mount that's gone next login; the file itself stays.
	const char* appimage = getenv("APPIMAGE");
	if (appimage && *appimage) target = appimage;
#endif
	if (!WriteWhole(path, LoginEntryText(target))) return false;
	fprintf(stderr, "[Console] login entry written: %s\n", path.c_str());
	return true;
}

void Host_SetEnv(const char* name, const char* value) {
	if (value) setenv(name, value, 1);
	else unsetenv(name);
}

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
