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
    int mainRequest = -1;  // select this tab of the main bar at the next frame (o tab ...): a player tab or Advanced
    int tabRequest = -1;   // and this technical tab inside Advanced
    // UI state.
    std::string saveResult;
    bool logFollow = true;
};

// "o tab <name>": status, colour, neck, tuner, debug, window, log (these open Advanced), advanced, or the label of a
// player tab (lower case, spaces as '-': seventh-string, guitar, menu). "player-<name>" always means the player tab
// (player-tuner; plain "tuner" is the technical tab). Sets the requests; false if the name is unknown.
bool Rs7OverlayUi_RequestTab(Rs7OverlayUi& ui, const char* name);
// *open = false when the window's close button was clicked.
void Rs7OverlayUi_Draw(Rs7OverlayUi& ui, bool* open);
