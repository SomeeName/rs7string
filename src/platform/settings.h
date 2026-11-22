#pragma once
#include "platform/config.h"
#include <cstdint>
#include <string>

// Every rsxr.ini key, as the in-game overlay (overlay/overlay.h) and the pipe commands
// ("o set <key> <value>", overlay/overlaycmd.h) see it: its value, the value the game runs with now,
// the value in the ini, and when a change takes effect.
//
// The keys are not listed here: each feature owns a settings table (RsxrSetTable, features/<name>/settings.cpp) with
// its keys, defaults, overlay labels and help, and its typed startup read. kRsxrSettingTables (src/settings_tables.cpp)
// lists every table once; its order is the order of the keys (RsxrSet_Def, Save) and, per ini section, of the release
// template (scripts/package.ps1 -TemplateOnly, docs/release/SETTINGS.md).
//
//  Live         the change reaches the game at once (a global the hooks read every call/frame, or a
//               setter that hands it to the render/game thread safely).
//  NextSession  read at the start of a session (no key uses it at the moment).
//  Restart      read once at startup (sites written or not, hooks installed or not, pools sized):
//               the new value is only saved, the running one stays until the game is restarted.
//
// Threads: RsxrSet_InitFromIni runs once on the startup worker before the overlay exists; everything
// else (Commit, SetText, Save, the getters) on the render thread (the overlay's Present hook drains
// the pipe commands there). Unit tests call them directly.

enum class RsxrSetType : uint8_t { Bool, Int, Float, Rgb, Text };
enum class RsxrSetWhen : uint8_t { Live, NextSession, Restart };

constexpr int kRsxrSetTextMax = kRsxrIniValueMax + 1;  // the longest ini value and its terminator (platform/config.h)

struct RsxrSetValue {
    int i = 0;                        // Bool (0/1), Int
    float f[1] = {0};                 // Float
    uint32_t rgb = 0;                 // Rgb, 0xRRGGBB
    char text[kRsxrSetTextMax] = {};  // Text (ASCII)
};

bool RsxrSet_Equal(RsxrSetType t, const RsxrSetValue& a, const RsxrSetValue& b);

struct RsxrSetDef {
    const char* id;  // "section.key", e.g. "neck.spacing_guitar7"
    const wchar_t* section;
    const wchar_t* key;
    RsxrSetType type;
    RsxrSetWhen when;
    const char* def;  // default, as ini text
    float lo, hi;     // Int / Float range (inclusive)
    const char* label;
    const char* help;
    void (*apply)(const RsxrSetValue&);  // Live / NextSession: hands the value to the game
    // Optional: the value the feature's own startup read produced (RsxrSetTable::read). RsxrSet_InitFromIni uses it
    // instead of the generic parse for keys the feature validates itself (e.g. string6_color_cb follows string6_color).
    void (*startup)(RsxrSetValue&) = nullptr;
};

// One feature's keys, in their order, and its typed startup read.
struct RsxrSetTable {
    const RsxrSetDef* defs;  // may be nullptr (count 0): a table with only a read
    int count;
    // Optional: reads the feature's startup values (its own validation and log lines) from a resolved ini path;
    // nullptr = the file is missing: the defaults, nothing read.
    void (*read)(const wchar_t* iniPath);
};

// Every table, in order (src/settings_tables.cpp).
extern const RsxrSetTable* const kRsxrSettingTables[];
extern const int kRsxrSettingTableCount;

// Runs every table's read in table order (RsxrSet_InitFromIni, once at startup). Returns whether the file
// exists. An invalid path is logged and every table gets its defaults.
bool RsxrSet_ReadIni(const wchar_t* iniPath);

// Reads the Bool / Int key of row `id` of `t` with that row's default and upper bound (RsxrConfig_ReadInt: invalid or
// out of range = 0), so a feature's startup read does not repeat the table's default. A missing row logs and returns 0.
int RsxrSet_ReadTableInt(const RsxrSetTable& t, const char* id, const wchar_t* iniPath);
// Parses the ini text of the Float row `id` of `t` with the overlay's rules (the whole text a number, within the row's
// range). True: *out = the number. False (empty, invalid, out of range or no such row): *out = the row's default.
bool RsxrSet_ParseTableFloat(const RsxrSetTable& t, const char* id, const wchar_t* text, float* out);

struct RsxrSetState {
    RsxrSetValue value;    // the overlay's value (what Save writes)
    RsxrSetValue running;  // what the game uses now (== value for Live keys)
    RsxrSetValue saved;    // what the ini holds (as loaded, or as last saved)
    bool fromIni = false;  // the key was present in the ini at load
};

int RsxrSet_Count();
const RsxrSetDef& RsxrSet_Def(int i);
RsxrSetState& RsxrSet_State(int i);
// "section.key" or a bare key that is unique across sections; -1 if unknown.
int RsxrSet_Find(const char* id);

// Loads every key once at startup (missing file or key: the default; invalid text: the default, as the feature's
// startup read): runs every table's read (RsxrSet_ReadIni), then running = value = saved. Applies nothing: Rsxr_Startup
// sets the globals from the store and the features' reads afterwards. Returns whether the file exists.
bool RsxrSet_InitFromIni(const wchar_t* iniPath);
// Back to the built-in defaults (tests), nothing applied.
void RsxrSet_ResetForTests();

enum class RsxrSetResult : uint8_t { Live, NextSession, Restart, Unchanged, Invalid, Unknown };
const char* RsxrSet_ResultName(RsxrSetResult r);
// Validates (range, type) and sets the value; Live / NextSession keys are applied (running = value).
RsxrSetResult RsxrSet_Commit(int i, const RsxrSetValue& v);
// Parses `text` for key `id` (same rules as the ini: bool 0/1, rgb "RRGGBB" with optional '#', vec3
// "x,y,z", floats in range) and commits it.
RsxrSetResult RsxrSet_SetText(const char* id, const char* text);
// Text of a value as the ini stores it ("1", "0.400", "C8C8C8", "0,0,0", ...).
std::string RsxrSet_Format(int i, const RsxrSetValue& v);

// value != saved.
bool RsxrSet_Dirty(int i);
int RsxrSet_DirtyCount();
// A Restart key whose value differs from the running one.
bool RsxrSet_NeedsRestart(int i);
// Writes every dirty key into the ini (WritePrivateProfileString keeps the other keys and comments);
// saved = value. Returns the number of keys written, -1 on a write error (the keys before it stay saved).
int RsxrSet_Save(const wchar_t* iniPath);

// Typed getters by id (the overlay's own keys); the default when the id is unknown.
bool RsxrSet_Bool(const char* id);
int RsxrSet_Int(const char* id);
float RsxrSet_Float(const char* id);
uint32_t RsxrSet_Rgb(const char* id);
const char* RsxrSet_Text(const char* id);
