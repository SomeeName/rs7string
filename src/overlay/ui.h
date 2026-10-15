#pragma once
#include <windows.h>
#include <string>

// The overlay's window (overlay.cpp calls it between ImGui::NewFrame and ImGui::Render).
struct Rs7OverlayUi {
    // Set by overlay.cpp every frame.
    const wchar_t* iniPath = L"";
    const wchar_t* gameDir = L"";
    int hotkey = 0;
    HWND hwnd = nullptr;
    unsigned long renderTid = 0, wndTid = 0;
    // Owned by overlay.cpp.
    float appliedScale = -1.0f;
    int tabRequest = -1;  // select this tab at the next frame (o tab ...)
    // UI state.
    std::string saveResult;
    unsigned long dumpArmedFrame = 0;
    bool logFollow = true;
};

// Tab names for "o tab <name>": status, colour, neck, tuner, debug, window, log. -1 if unknown.
int Rs7OverlayUi_TabIndex(const char* name);
// *open = false when the window's close button was clicked.
void Rs7OverlayUi_Draw(Rs7OverlayUi& ui, bool* open);
