#pragma once
#include <cstdint>
#include <string>

// Every rs7string.ini key, as the in-game overlay (overlay/overlay.h) and the pipe commands
// ("o set <key> <value>", core/overlaycmd.h) see it: its value, the value the game runs with now,
// the value in the ini, and when a change takes effect.
//
//  Live         the change reaches the game at once (a global the hooks read every call/frame, or a
//               setter that hands it to the render/game thread safely).
//  NextSession  read at the start of a session (no key uses it at the moment; the former tuner seven_slot became
//  Restart, review F2). Restart      read once at startup (sites written or not, hooks installed or not, pools sized):
//               the new value is only saved, the running one stays until the game is restarted.
//
// Threads: Rs7Set_InitFromIni runs once on the startup worker before the overlay exists; everything
// else (Commit, SetText, Save, the getters) on the render thread (the overlay's Present hook drains
// the pipe commands there). Unit tests call them directly.

enum class Rs7SetType : uint8_t { Bool, Int, Float, Rgb, Text };
enum class Rs7SetWhen : uint8_t { Live, NextSession, Restart };

constexpr int kRs7SetTextMax = 260;

struct Rs7SetValue {
    int i = 0;                       // Bool (0/1), Int
    float f[1] = {0};                // Float
    uint32_t rgb = 0;                // Rgb, 0xRRGGBB
    char text[kRs7SetTextMax] = {};  // Text (ASCII)
};

bool Rs7Set_Equal(Rs7SetType t, const Rs7SetValue& a, const Rs7SetValue& b);

struct Rs7SetDef {
    const char* id;  // "section.key", e.g. "neck.string7_v"
    const wchar_t* section;
    const wchar_t* key;
    Rs7SetType type;
    Rs7SetWhen when;
    const char* def;  // default, as ini text
    float lo, hi;     // Int / Float range (inclusive)
    const char* label;
    const char* help;
    void (*apply)(const Rs7SetValue&);  // Live / NextSession: hands the value to the game
};

struct Rs7SetState {
    Rs7SetValue value;     // the overlay's value (what Save writes)
    Rs7SetValue running;   // what the game uses now (== value for Live keys)
    Rs7SetValue saved;     // what the ini holds (as loaded, or as last saved)
    bool fromIni = false;  // the key was present in the ini at load
};

int Rs7Set_Count();
const Rs7SetDef& Rs7Set_Def(int i);
Rs7SetState& Rs7Set_State(int i);
// "section.key" or a bare key that is unique across sections; -1 if unknown.
int Rs7Set_Find(const char* id);

// Loads every key (missing file or key: the default; invalid text: the default, as config.cpp).
// running = value = saved. Applies nothing: Rs7_Startup has set the globals from the same ini.
void Rs7Set_InitFromIni(const wchar_t* iniPath);
// Back to the built-in defaults (tests), nothing applied.
void Rs7Set_ResetForTests();

enum class Rs7SetResult : uint8_t { Live, NextSession, Restart, Unchanged, Invalid, Unknown };
const char* Rs7Set_ResultName(Rs7SetResult r);
// Validates (range, type) and sets the value; Live / NextSession keys are applied (running = value).
Rs7SetResult Rs7Set_Commit(int i, const Rs7SetValue& v);
// Parses `text` for key `id` (same rules as the ini: bool 0/1, rgb "RRGGBB" with optional '#', vec3
// "x,y,z", floats in range) and commits it.
Rs7SetResult Rs7Set_SetText(const char* id, const char* text);
// Text of a value as the ini stores it ("1", "0.400", "C8C8C8", "0,0,0", ...).
std::string Rs7Set_Format(int i, const Rs7SetValue& v);

// value != saved.
bool Rs7Set_Dirty(int i);
int Rs7Set_DirtyCount();
// A Restart key whose value differs from the running one.
bool Rs7Set_NeedsRestart(int i);
// Writes every dirty key into the ini (WritePrivateProfileString keeps the other keys and comments);
// saved = value. Returns the number of keys written, -1 on a write error (the keys before it stay saved).
int Rs7Set_Save(const wchar_t* iniPath);

// Typed getters by id (the overlay's own keys); the default when the id is unknown.
bool Rs7Set_Bool(const char* id);
int Rs7Set_Int(const char* id);
float Rs7Set_Float(const char* id);
const char* Rs7Set_Text(const char* id);
