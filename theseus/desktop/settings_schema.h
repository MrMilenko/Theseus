// settings_schema.h: one table describes every desktop.ini setting, and
// load, save and the settings screens all work from it.
#pragma once
#include <stddef.h>

enum SettingKind { SK_BOOL, SK_ENUM, SK_INT, SK_FLOAT, SK_PATH, SK_STRING };

enum SettingFlags {
    SF_RESTART = 1 << 0,  // takes effect on next launch
    SF_SECRET  = 1 << 1,  // tokens and keys: never shown, never copied
    SF_HIDDEN  = 1 << 2,  // kept in the file, left off the settings screens
    SF_SHARED  = 1 << 3,  // the dashboard writes it too; the file wins unless we changed it
    SF_STRICT  = 1 << 4,  // enum: refuse values that aren't in the list
    SF_PERCENT = 1 << 5,  // 0..1 float shown as a percentage
    SF_CLAMP   = 1 << 6,  // pull loaded values into minV..maxV
};

struct SettingDef {
    const char* key;      // name in the file
    const char* section;  // [section] it's written under
    const char* page;     // settings screen it shows on (General, Display...)
    const char* label;
    SettingKind kind;
    void*       ptr;      // bool / int / float / char[size]
    int         size;     // char buffer size for paths and strings
    const char* def;      // default, as file text
    unsigned    flags;
    // Enums: values[] are the stored ints, labels[] what you see, tokens[]
    // the file text when it isn't just the number.
    int                 count;
    const int*          values;
    const char* const*  labels;
    const char* const*  tokens;
    float       minV, maxV, step;  // UI range and step; enum: loose load range
    const char* fmt;      // float file format, "%.3f" when null
    void      (*apply)(); // runs after a change from a settings screen
};

struct SettingsTable {
    const SettingDef* defs;
    int count;
};

// The desktop table (desktop_settings.cpp).
const SettingsTable& DesktopSettings_Table();
const SettingDef*    DesktopSettings_Find(const char* key);

// Text <-> value. Parse returns false and leaves the value alone when the
// text isn't acceptable.
bool Setting_Parse(const SettingDef& d, const char* text);
void Setting_Format(const SettingDef& d, char* out, size_t cch);
void Setting_Display(const SettingDef& d, char* out, size_t cch);
int  Setting_EnumIndex(const SettingDef& d);
bool Setting_IsDefault(const SettingDef& d);

// Step a bool / enum / int / float. dir is +1 or -1. Returns true on change.
bool Setting_Cycle(const SettingDef& d, int dir);
bool Setting_SetText(const SettingDef& d, const char* text);
void Setting_Reset(const SettingDef& d);

// Reads every known key; the rest of the file is ignored.
bool Settings_Load(const SettingsTable& t, const char* path);

// Rewrites known keys in place and keeps every other line (unknown keys,
// comments, the dashboard's own sections). Keys missing from the file land
// at the end of their section.
bool Settings_Save(const SettingsTable& t, const char* path);
