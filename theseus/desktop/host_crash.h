// host_crash.h: text helpers the crash handlers in host_posix_app.cpp and
// host_win_app.cpp share. No allocation, no stdio, nothing that takes a lock,
// so the POSIX side can use them inside a signal handler.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
	CRASH_TEXT_SIZE = 128 * 1024,
	CRASH_LOG_READ  = 64 * 1024,   // the tail of theseus.log we look at
	CRASH_LOG_LINES = 200,
	CRASH_KEEP      = 20,          // reports kept in crashes/
};

struct CrashText {
	char data[CRASH_TEXT_SIZE];
	size_t len;
};

static inline void CrashAppendN(CrashText* t, const char* s, size_t n) {
	if (n > sizeof(t->data) - 1 - t->len) n = sizeof(t->data) - 1 - t->len;
	memcpy(t->data + t->len, s, n);
	t->len += n;
	t->data[t->len] = '\0';
}

static inline void CrashAppend(CrashText* t, const char* s) {
	if (s) CrashAppendN(t, s, strlen(s));
}

static inline void CrashAppendDec(CrashText* t, uint64_t v, int width = 0) {
	char buf[24];
	int n = 0;
	do { buf[n++] = (char)('0' + v % 10); v /= 10; } while (v);
	while (n < width) buf[n++] = '0';
	char out[24];
	for (int i = 0; i < n; i++) out[i] = buf[n - 1 - i];
	CrashAppendN(t, out, (size_t)n);
}

static inline void CrashAppendHex(CrashText* t, uint64_t v) {
	char out[19] = "0x";
	int n = 0;
	char buf[16];
	do { buf[n++] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v);
	for (int i = 0; i < n; i++) out[2 + i] = buf[n - 1 - i];
	CrashAppendN(t, out, (size_t)(2 + n));
}

// "2026-11-12 14:03:22" (sep ' ', ':') or "2026-11-12_14-03-22" for a file
// name, from seconds since 1970 already shifted to local time.
static inline void CrashAppendTime(CrashText* t, int64_t secs, bool forFile) {
	int64_t days = secs / 86400, rem = secs % 86400;
	if (rem < 0) { rem += 86400; days--; }
	// Howard Hinnant's days-to-civil.
	days += 719468;
	int64_t era = (days >= 0 ? days : days - 146096) / 146097;
	int64_t doe = days - era * 146097;
	int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t y = yoe + era * 400;
	int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int64_t mp = (5 * doy + 2) / 153;
	int64_t d = doy - (153 * mp + 2) / 5 + 1;
	int64_t m = mp < 10 ? mp + 3 : mp - 9;
	if (m <= 2) y++;
	CrashAppendDec(t, (uint64_t)y, 4);   CrashAppend(t, "-");
	CrashAppendDec(t, (uint64_t)m, 2);   CrashAppend(t, "-");
	CrashAppendDec(t, (uint64_t)d, 2);   CrashAppend(t, forFile ? "_" : " ");
	CrashAppendDec(t, (uint64_t)(rem / 3600), 2);       CrashAppend(t, forFile ? "-" : ":");
	CrashAppendDec(t, (uint64_t)(rem / 60 % 60), 2);    CrashAppend(t, forFile ? "-" : ":");
	CrashAppendDec(t, (uint64_t)(rem % 60), 2);
}

// Where the last `lines` lines of buf start. When buf began mid-file
// (partial), the first, cut-off line is dropped.
static inline size_t CrashTailStart(const char* buf, size_t len, int lines, bool partial) {
	size_t end = len;
	if (end && buf[end - 1] == '\n') end--;
	size_t i = end;
	while (i > 0) {
		if (buf[i - 1] == '\n' && --lines == 0) return i;
		i--;
	}
	if (!partial) return 0;
	const char* nl = (const char*)memchr(buf, '\n', len);
	return nl ? (size_t)(nl - buf) + 1 : len;
}

static inline char CrashLower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

// Copies in to out with every home folder path swapped for "~". Windows
// paths match regardless of case and slash direction.
static inline void CrashRedact(CrashText* out, const char* in, size_t len, const char* home, bool loose) {
	size_t hl = home ? strlen(home) : 0;
	while (hl > 1 && (home[hl - 1] == '/' || home[hl - 1] == '\\')) hl--;
	size_t i = 0;
	while (i < len) {
		bool hit = hl > 1 && i + hl <= len;
		for (size_t k = 0; hit && k < hl; k++) {
			char a = in[i + k], b = home[k];
			if (loose) {
				if (a == '\\') a = '/';
				if (b == '\\') b = '/';
				a = CrashLower(a);
				b = CrashLower(b);
			}
			hit = a == b;
		}
		if (hit) {
			CrashAppendN(out, "~", 1);
			i += hl;
		} else {
			CrashAppendN(out, in + i, 1);
			i++;
		}
	}
}
