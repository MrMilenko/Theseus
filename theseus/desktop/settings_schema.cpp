// settings_schema.cpp: load, save and edit settings through a SettingsTable.

#include "settings_schema.h"
#include "safe_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>

static int EnumIndexOf(const SettingDef& d, int v)
{
    for (int i = 0; i < d.count; i++)
        if (d.values[i] == v) return i;
    return -1;
}

// A token can carry aliases after '|'; the first one is what gets written.
static size_t TokenMatch(const char* tok, const char* text)
{
    size_t best = 0;
    while (tok && *tok) {
        const char* bar = strchr(tok, '|');
        size_t n = bar ? (size_t)(bar - tok) : strlen(tok);
        if (n > best && strncmp(text, tok, n) == 0) best = n;
        tok = bar ? bar + 1 : NULL;
    }
    return best;
}

static void TokenFirst(const char* tok, char* out, size_t cch)
{
    const char* bar = strchr(tok, '|');
    size_t n = bar ? (size_t)(bar - tok) : strlen(tok);
    if (n >= cch) n = cch - 1;
    memcpy(out, tok, n);
    out[n] = 0;
}

static float Clamp(const SettingDef& d, float v)
{
    if (d.minV < d.maxV) {
        if (v < d.minV) v = d.minV;
        if (v > d.maxV) v = d.maxV;
    }
    return v;
}

bool Setting_Parse(const SettingDef& d, const char* text)
{
    switch (d.kind) {
    case SK_BOOL:
        *(bool*)d.ptr = atoi(text) != 0;
        return true;
    case SK_INT: {
        int n = atoi(text);
        if (d.flags & SF_CLAMP) n = (int)Clamp(d, (float)n);
        *(int*)d.ptr = n;
        return true;
    }
    case SK_FLOAT: {
        float v = (float)atof(text);
        *(float*)d.ptr = (d.flags & SF_CLAMP) ? Clamp(d, v) : v;
        return true;
    }
    case SK_PATH:
    case SK_STRING: {
        char* buf = (char*)d.ptr;
        strncpy(buf, text, d.size - 1);
        buf[d.size - 1] = 0;
        return true;
    }
    case SK_ENUM: {
        if (d.tokens) {
            int pick = -1, blank = -1;
            size_t best = 0;
            for (int i = 0; i < d.count; i++) {
                if (!d.tokens[i][0]) { blank = i; continue; }
                size_t m = TokenMatch(d.tokens[i], text);
                if (m > best) { best = m; pick = i; }
            }
            if (pick < 0) pick = blank;
            if (pick < 0) return false;
            *(int*)d.ptr = d.values[pick];
            return true;
        }
        int n = atoi(text);
        if (EnumIndexOf(d, n) >= 0 ||
            (!(d.flags & SF_STRICT) && d.minV < d.maxV && n >= d.minV && n <= d.maxV)) {
            *(int*)d.ptr = n;
            return true;
        }
        return false;
    }
    }
    return false;
}

void Setting_Format(const SettingDef& d, char* out, size_t cch)
{
    switch (d.kind) {
    case SK_BOOL:  snprintf(out, cch, "%d", *(bool*)d.ptr ? 1 : 0); break;
    case SK_INT:   snprintf(out, cch, "%d", *(int*)d.ptr); break;
    case SK_FLOAT: snprintf(out, cch, d.fmt ? d.fmt : "%.3f", *(float*)d.ptr); break;
    case SK_PATH:
    case SK_STRING: snprintf(out, cch, "%s", (const char*)d.ptr); break;
    case SK_ENUM: {
        int v = *(int*)d.ptr;
        int i = EnumIndexOf(d, v);
        if (d.tokens && i >= 0) TokenFirst(d.tokens[i], out, cch);
        else snprintf(out, cch, "%d", v);
        break;
    }
    }
}

int Setting_EnumIndex(const SettingDef& d)
{
    if (d.kind != SK_ENUM) return -1;
    return EnumIndexOf(d, *(int*)d.ptr);
}

void Setting_Display(const SettingDef& d, char* out, size_t cch)
{
    switch (d.kind) {
    case SK_BOOL: snprintf(out, cch, "%s", *(bool*)d.ptr ? "On" : "Off"); break;
    case SK_ENUM: {
        int i = Setting_EnumIndex(d);
        if (i >= 0 && d.labels) snprintf(out, cch, "%s", d.labels[i]);
        else snprintf(out, cch, "%d", *(int*)d.ptr);
        break;
    }
    case SK_INT: snprintf(out, cch, "%d", *(int*)d.ptr); break;
    case SK_FLOAT:
        if (d.flags & SF_PERCENT)
            snprintf(out, cch, "%d%%", (int)(*(float*)d.ptr * 100.0f + 0.5f));
        else
            snprintf(out, cch, "%.2f", *(float*)d.ptr);
        break;
    case SK_PATH:
    case SK_STRING:
        if (d.flags & SF_SECRET) snprintf(out, cch, "%s", ((const char*)d.ptr)[0] ? "Set" : "Not set");
        else snprintf(out, cch, "%s", ((const char*)d.ptr)[0] ? (const char*)d.ptr : "Not set");
        break;
    }
}

bool Setting_IsDefault(const SettingDef& d)
{
    char cur[2048];
    Setting_Format(d, cur, sizeof(cur));
    return strcmp(cur, d.def ? d.def : "") == 0;
}

bool Setting_Cycle(const SettingDef& d, int dir)
{
    switch (d.kind) {
    case SK_BOOL:
        *(bool*)d.ptr = !*(bool*)d.ptr;
        return true;
    case SK_ENUM: {
        if (d.count <= 1) return false;
        int i = Setting_EnumIndex(d);
        if (i < 0) i = dir > 0 ? -1 : 0;
        i = (i + dir + d.count) % d.count;
        *(int*)d.ptr = d.values[i];
        return true;
    }
    case SK_INT: {
        int v = *(int*)d.ptr, n = v + (int)(d.step > 0 ? d.step : 1) * dir;
        if (d.minV < d.maxV) { if (n < d.minV) n = (int)d.minV; if (n > d.maxV) n = (int)d.maxV; }
        *(int*)d.ptr = n;
        return n != v;
    }
    case SK_FLOAT: {
        float v = *(float*)d.ptr;
        float st = d.step > 0 ? d.step : 0.05f;
        // Land on the step grid so repeated presses read as round numbers.
        float n = roundf((v + st * dir) / st) * st;
        if (d.minV < d.maxV) { if (n < d.minV) n = d.minV; if (n > d.maxV) n = d.maxV; }
        *(float*)d.ptr = n;
        return n != v;
    }
    default:
        return false;
    }
}

bool Setting_SetText(const SettingDef& d, const char* text)
{
    char before[2048];
    Setting_Format(d, before, sizeof(before));
    if (!Setting_Parse(d, text)) return false;
    char after[2048];
    Setting_Format(d, after, sizeof(after));
    return strcmp(before, after) != 0;
}

void Setting_Reset(const SettingDef& d)
{
    Setting_Parse(d, d.def ? d.def : "");
}

static const SettingDef* FindIn(const SettingsTable& t, const char* key, size_t n)
{
    for (int i = 0; i < t.count; i++)
        if (strlen(t.defs[i].key) == n && strncmp(t.defs[i].key, key, n) == 0)
            return &t.defs[i];
    return NULL;
}

static void StripEol(char* s)
{
    char* p = strchr(s, '\n'); if (p) *p = 0;
    p = strchr(s, '\r'); if (p) *p = 0;
}

static bool ReadLines(const char* path, std::vector<std::string>& lines)
{
    FILE* fp = fopen(path, "r");
    if (!fp) return false;
    char line[4096];   // xCloud refresh tokens run long
    while (fgets(line, sizeof(line), fp)) {
        StripEol(line);
        lines.push_back(line);
    }
    fclose(fp);
    return true;
}

// What a shared row held when we last read or wrote the file, so a save can
// tell our change from the dashboard's.
struct SyncedValue { const SettingDef* def; std::string text; };
static std::vector<SyncedValue> s_synced;

static void NoteSynced(const SettingDef& d)
{
    char v[2048];
    Setting_Format(d, v, sizeof(v));
    for (size_t i = 0; i < s_synced.size(); i++)
        if (s_synced[i].def == &d) { s_synced[i].text = v; return; }
    SyncedValue sv = { &d, v };
    s_synced.push_back(sv);
}

static bool ChangedHere(const SettingDef& d)
{
    char v[2048];
    Setting_Format(d, v, sizeof(v));
    for (size_t i = 0; i < s_synced.size(); i++)
        if (s_synced[i].def == &d) return s_synced[i].text != v;
    return !Setting_IsDefault(d);
}

bool Settings_Load(const SettingsTable& t, const char* path)
{
    std::vector<std::string> lines;
    if (!ReadLines(path, lines)) return false;
    for (size_t i = 0; i < lines.size(); i++) {
        const char* s = lines[i].c_str();
        const char* eq = strchr(s, '=');
        if (!eq) continue;
        const SettingDef* d = FindIn(t, s, (size_t)(eq - s));
        if (d) Setting_Parse(*d, eq + 1);
    }
    for (int i = 0; i < t.count; i++)
        if (t.defs[i].flags & SF_SHARED) NoteSynced(t.defs[i]);
    return true;
}

static std::string SectionOf(const std::string& line)
{
    if (line.size() < 2 || line[0] != '[') return std::string();
    size_t close = line.find(']');
    return close == std::string::npos ? std::string() : line.substr(1, close - 1);
}

static bool IsBlank(const std::string& s)
{
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] != ' ' && s[i] != '\t') return false;
    return true;
}

bool Settings_Save(const SettingsTable& t, const char* path)
{
    std::vector<std::string> in, out;
    ReadLines(path, in);

    std::vector<char> written(t.count, 0);
    // Where each section's first block ends: after its last non-blank line.
    std::vector<std::string> secNames;
    std::vector<size_t> secEnd;
    int curSec = -1;
    char val[4096];

    for (size_t i = 0; i < in.size(); i++) {
        const std::string& line = in[i];
        std::string sec = SectionOf(line);
        if (!sec.empty()) {
            curSec = -1;
            for (size_t k = 0; k < secNames.size(); k++)
                if (secNames[k] == sec) { curSec = -2; break; }  // repeat block, don't extend
            if (curSec == -1) {
                secNames.push_back(sec);
                secEnd.push_back(out.size() + 1);
                curSec = (int)secNames.size() - 1;
            }
            out.push_back(line);
            continue;
        }

        size_t eq = line.find('=');
        const SettingDef* d = (eq != std::string::npos && line[0] != ';' && line[0] != '#')
                            ? FindIn(t, line.c_str(), eq) : NULL;
        if (d) {
            int idx = (int)(d - t.defs);
            if (written[idx]) continue;   // a duplicate would shadow the real value
            written[idx] = 1;
            if ((d->flags & SF_SHARED) && !ChangedHere(*d)) {
                Setting_Parse(*d, line.c_str() + eq + 1);
                out.push_back(line);
            } else {
                Setting_Format(*d, val, sizeof(val));
                out.push_back(std::string(d->key) + "=" + val);
            }
        } else {
            out.push_back(line);
        }
        if (curSec >= 0 && !IsBlank(out.back())) secEnd[curSec] = out.size();
    }

    // Missing keys go at the end of their section, or a new one at the end.
    struct Insert { size_t at; std::vector<std::string> lines; };
    std::vector<Insert> inserts;
    std::vector<std::string> newSecs;
    std::vector<std::vector<std::string> > newSecLines;
    for (int i = 0; i < t.count; i++) {
        if (written[i]) continue;
        const SettingDef& d = t.defs[i];
        Setting_Format(d, val, sizeof(val));
        std::string kv = std::string(d.key) + "=" + val;

        int found = -1;
        for (size_t k = 0; k < secNames.size(); k++)
            if (secNames[k] == d.section) { found = (int)k; break; }
        if (found >= 0) {
            Insert* ins = NULL;
            for (size_t k = 0; k < inserts.size(); k++)
                if (inserts[k].at == secEnd[found]) ins = &inserts[k];
            if (!ins) { inserts.push_back(Insert()); ins = &inserts.back(); ins->at = secEnd[found]; }
            ins->lines.push_back(kv);
            continue;
        }
        int ns = -1;
        for (size_t k = 0; k < newSecs.size(); k++)
            if (newSecs[k] == d.section) { ns = (int)k; break; }
        if (ns < 0) {
            newSecs.push_back(d.section);
            newSecLines.push_back(std::vector<std::string>());
            ns = (int)newSecs.size() - 1;
        }
        newSecLines[ns].push_back(kv);
    }

    // Back to front so earlier positions stay valid.
    for (size_t a = 0; a < inserts.size(); a++)
        for (size_t b = a + 1; b < inserts.size(); b++)
            if (inserts[b].at > inserts[a].at) std::swap(inserts[a], inserts[b]);
    for (size_t k = 0; k < inserts.size(); k++)
        out.insert(out.begin() + inserts[k].at, inserts[k].lines.begin(), inserts[k].lines.end());

    for (size_t k = 0; k < newSecs.size(); k++) {
        if (!out.empty() && !IsBlank(out.back())) out.push_back("");
        out.push_back("[" + newSecs[k] + "]");
        out.insert(out.end(), newSecLines[k].begin(), newSecLines[k].end());
    }

    for (int i = 0; i < t.count; i++)
        if (t.defs[i].flags & SF_SHARED) NoteSynced(t.defs[i]);

    SafeSave save(path);
    if (!save.f) return false;
    for (size_t i = 0; i < out.size(); i++)
        fprintf(save.f, "%s\n", out[i].c_str());
    return save.Commit();
}
