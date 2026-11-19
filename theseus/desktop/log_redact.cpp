// log_redact.cpp: home folder, IP address and sign-in token masking for copied logs.

#include "log_redact.h"
#include <string.h>
#include <ctype.h>
#include <strings.h>

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

// Sign-in tokens in URLs and auth headers: api_key=..., X-Plex-Token=...,
// Token="...". The name stays so a log still says what was sent.
static const char* const kTokenKeys[] = {
    "api_key", "apikey", "x-plex-token", "x-emby-token", "access_token",
    "accesstoken", "token", "refresh_token", "password", "pw",
};

static bool TokenKeyAt(const std::string& s, size_t i, size_t* keyLen)
{
    for (size_t k = 0; k < sizeof(kTokenKeys) / sizeof(kTokenKeys[0]); k++) {
        size_t n = strlen(kTokenKeys[k]);
        if (i + n > s.size() || strncasecmp(s.c_str() + i, kTokenKeys[k], n) != 0) continue;
        // Whole words only, so "devicetoken" or "tokenizer" aren't touched.
        if (i > 0 && (isalnum((unsigned char)s[i - 1]) || s[i - 1] == '_' || s[i - 1] == '-')) continue;
        char c = i + n < s.size() ? s[i + n] : 0;
        if (c == '=' || c == ':' || c == '"') { *keyLen = n; return true; }
    }
    return false;
}

static std::string MaskTokens(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        size_t n;
        if (!TokenKeyAt(in, i, &n)) { out += in[i++]; continue; }
        out.append(in, i, n);
        i += n;
        // Separator: = or : with optional quotes and spaces around it.
        size_t j = i;
        while (j < in.size() && (in[j] == '"' || in[j] == '\\')) j++;
        if (j >= in.size() || (in[j] != '=' && in[j] != ':')) { continue; }
        j++;
        while (j < in.size() && (in[j] == ' ' || in[j] == '"' || in[j] == '\'')) j++;
        size_t v = j;
        while (v < in.size() && !strchr("&\"' \t\r\n,;}>)", in[v])) v++;
        out.append(in, i, j - i);
        if (v > j) out += "[redacted]";
        i = v;
    }
    return out;
}

std::string Log_Redact(const std::string& raw, const char* home)
{
    std::string in = MaskTokens(raw);
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
