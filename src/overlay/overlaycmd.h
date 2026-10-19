#pragma once
#include <string>

// Overlay commands for background test driving: lines "o ..." on the DirectInput pipe
// \\.\pipe\rs7string (src/dinput_wrap.h; tools/rsoverlay.py). The pipe thread posts them, the overlay's
// Present hook (render thread) runs them, so they need [overlay] enabled=1 (and [window]
// background=1 for the pipe). Every command logs one "overlay: cmd ..." line with its result.
//
//   o toggle | o show | o hide        overlay visibility
//   o tab <status|colour|neck|tuner|debug|window|log>   select a tab
//   o set <key> <value>               like editing it in the overlay ("neck.string7_v 0.5", a bare key
//                                     works when unique; the value is the rest of the line)
//   o save                            write the changed keys into rs7string.ini
//   o status                          log a status line (frames, visibility, dirty keys)

enum class Rs7CmdKind { Toggle, Show, Hide, Tab, Set, Save, Status };

struct Rs7Cmd {
    Rs7CmdKind kind = Rs7CmdKind::Status;
    std::string key;    // Tab: the tab name; Set: the key
    std::string value;  // Set: the value text
};

// "o set neck.string7_v 0.5" -> {Set, "neck.string7_v", "0.5"}. False for anything else (lines not
// starting with "o ", unknown verbs, missing arguments).
bool Rs7Cmd_Parse(const char* line, Rs7Cmd* out);

// Pipe thread: parse and queue (at most 64 waiting; more are dropped and logged). False: not queued.
bool Rs7Cmd_Post(const char* line);
// Render thread: the oldest queued command. False: none.
bool Rs7Cmd_Pop(Rs7Cmd* out);

// "F9", "f9", "INSERT", "0x78", ... -> virtual-key code; 0 for anything else. Names: F1-F24, INSERT,
// DELETE, HOME, END, PGUP, PGDN, PAUSE, SCROLL, NUMLOCK, OEM_3 (the key left of 1 on US layouts), A-Z, 0-9.
int Rs7_ParseHotkey(const char* text);
