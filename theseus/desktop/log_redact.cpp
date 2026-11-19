// log_redact.cpp: token, home folder and IP address masking for logs.

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

static bool EndsWithKey(const std::string& s, size_t end, const char* key)
{
    size_t n = strlen(key);
    if (end < n) return false;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)s[end - n + i]) != key[i]) return false;
    return true;
}

// A key that names a secret: anything ending in token, plus api_key / apikey.
static bool SecretKeyEndsAt(const std::string& s, size_t end)
{
    return EndsWithKey(s, end, "token") || EndsWithKey(s, end, "api_key") ||
           EndsWithKey(s, end, "apikey") || EndsWithKey(s, end, "secret");
}

std::string Log_RedactSecrets(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        char c = in[i];
        size_t v = std::string::npos;
        // key=value (query strings, Token="..." in auth headers)
        if (c == '=' && SecretKeyEndsAt(in, i)) v = i + 1;
        // "key": "value" (JSON)
        if (c == '"' && i > 0 && SecretKeyEndsAt(in, i)) {
            size_t j = i + 1;
            while (j < in.size() && in[j] == ' ') j++;
            if (j < in.size() && in[j] == ':') {
                j++;
                while (j < in.size() && in[j] == ' ') j++;
                if (j < in.size() && in[j] == '"') v = j;
            }
        }
        if (v == std::string::npos) { out += c; i++; continue; }
        out.append(in, i, v - i);
        bool quoted = v < in.size() && in[v] == '"';
        size_t e = v + (quoted ? 1 : 0);
        while (e < in.size()) {
            char d = in[e];
            if (quoted ? d == '"' : (d == '&' || d == '"' || d == '\'' || d == ',' || isspace((unsigned char)d))) break;
            e++;
        }
        size_t start = v + (quoted ? 1 : 0);
        if (quoted) out += '"';
        if (e > start) out += "[redacted]";
        i = e;
    }
    return out;
}

std::string Log_Redact(const std::string& raw, const char* home)
{
    std::string in = Log_RedactSecrets(raw);
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
