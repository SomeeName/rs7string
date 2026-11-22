#pragma once
#include <string>

// Overlay commands for background test driving: lines "o ..." on the DirectInput pipe
// \\.\pipe\rsxr (src/dinput_wrap.h; tools/rsoverlay.py). The pipe thread posts them, the overlay's
// Present hook (render thread) runs them, so they need [overlay] enabled=1 (and [window]
// background=1 for the pipe). Every command logs one "overlay: cmd ..." line with its result.
//
//   o toggle | o show | o hide        overlay visibility
//   o tab <name>                      select a tab (names: RsxrOverlayUi_RequestTab, overlay/ui.h)
//   o set <key> <value>               like editing it in the overlay ("neck.spacing_guitar7 1.2", a bare key
//                                     works when unique; the value is the rest of the line)
//   o save                            write the changed keys into rsxr.ini
//   o status                          log a status line (frames, visibility, dirty keys)

enum class RsxrCmdKind { Toggle, Show, Hide, Tab, Set, Save, Status };

struct RsxrCmd {
    RsxrCmdKind kind = RsxrCmdKind::Status;
    std::string key;    // Tab: the tab name; Set: the key
    std::string value;  // Set: the value text
};

// "o set neck.spacing_guitar7 1.2" -> {Set, "neck.spacing_guitar7", "1.2"}. False for anything else (lines not
// starting with "o ", unknown verbs, missing arguments).
bool RsxrCmd_Parse(const char* line, RsxrCmd* out);

// Pipe thread: parse and queue (at most 64 waiting; more are dropped and logged). False: not queued.
bool RsxrCmd_Post(const char* line);
// Render thread: the oldest queued command. False: none.
bool RsxrCmd_Pop(RsxrCmd* out);

// "F9", "f9", "INSERT", "0x78", ... -> virtual-key code; 0 for anything else. Names: F1-F24, INSERT,
// DELETE, HOME, END, PGUP, PGDN, PAUSE, SCROLL, NUMLOCK, OEM_3 (the key left of 1 on US layouts), A-Z, 0-9.
int Rsxr_ParseHotkey(const char* text);
