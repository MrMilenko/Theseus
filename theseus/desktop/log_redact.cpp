// log_redact.cpp: home folder and IP address masking for copied logs.

#include "log_redact.h"
#include <string.h>
#include <ctype.h>

static bool IsHex(char c) { return isxdigit((unsigned char)c) != 0; }

// Length of an IPv4 address starting at p, or 0.
static size_t MatchV4(const char* p, bool* keep)
{
    const char* s = p;
    int parts[4];
    for (int i = 0; i < 4; i++) {
        if (!isdigit((unsigned char)*s)) return 0;
        int n = 0, digits = 0;
        while (isdigit((unsigned char)*s) && digits < 4) { n = n * 10 + (*s - '0'); s++; digits++; }
        if (digits > 3 || n > 255) return 0;
        parts[i] = n;
        if (i < 3) { if (*s != '.') return 0; s++; }
    }
    // Part of something longer, like a version string.
    if (isdigit((unsigned char)*s) || (*s == '.' && isdigit((unsigned char)s[1]))) return 0;
    *keep = (parts[0] == 127) || (parts[0] == 0 && parts[1] == 0 && parts[2] == 0 && parts[3] == 0);
    return (size_t)(s - p);
}

// Length of an IPv6 address starting at p, or 0. Needs "::" or seven colons,
// and a hex letter or "::", so times like 12:30:45 are left alone.
static size_t MatchV6(const char* p, bool* keep)
{
    const char* s = p;
    int colons = 0, group = 0;
    bool letter = false, dbl = false;
    while (IsHex(*s) || *s == ':') {
        if (*s == ':') {
            colons++;
            if (s[1] == ':') dbl = true;
            group = 0;
        } else {
            if (++group > 4) return 0;
            if (isalpha((unsigned char)*s)) letter = true;
        }
        s++;
    }
    if (colons < 2 || (!dbl && colons != 7) || (!dbl && !letter)) return 0;
    if (s - p < 3) return 0;
    std::string a(p, (size_t)(s - p));
    *keep = (a == "::1" || a == "::");
    return (size_t)(s - p);
}

std::string Log_Redact(const std::string& in, const char* home)
{
    std::string text;
    size_t hl = home ? strlen(home) : 0;
    while (hl > 1 && (home[hl - 1] == '/' || home[hl - 1] == '\\')) hl--;
    if (hl > 1) {
        for (size_t i = 0; i < in.size();) {
            if (in.compare(i, hl, home, hl) == 0) { text += '~'; i += hl; }
            else text += in[i++];
        }
    } else {
        text = in;
    }

    std::string out;
    out.reserve(text.size());
    const char* p = text.c_str();
    const char* end = p + text.size();
    while (p < end) {
        bool prevWord = (p > text.c_str()) && (isalnum((unsigned char)p[-1]) || p[-1] == '.' || p[-1] == ':');
        bool keep = false;
        size_t n = 0;
        if (!prevWord) {
            n = MatchV4(p, &keep);
            if (!n) n = MatchV6(p, &keep);
        }
        if (n) {
            if (keep) out.append(p, n);
            else out += "[ip]";
            p += n;
        } else {
            out += *p++;
        }
    }
    return out;
}
