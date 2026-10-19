#pragma once
#include <windows.h>

// In-game debug/test overlay (Dear ImGui, win32 + dx9 backends): every rs7string feature's state,
// live toggles where safe, "restart required" for startup-only keys, a status panel and the log tail,
// "save to rs7string.ini". On by default ([overlay] enabled=1) but hidden ([overlay] open=0) until the
// hotkey [overlay] hotkey (default F9) shows it; enabled=0 installs no overlay hooks at all.
//
// - Device: the ramp worker's device finder (d3d/worker.h) hands over the game's device; the overlay
//   patches its vtable slots 16/17 (d3d/present.h; no d3d9.dll import).
// - Drawing: in the Present hook, on the back buffer, inside its own BeginScene/EndScene, only while
//   visible. Own ImGui context (this DLL's copy of ImGui; RSMods has its own in xinput1_3.dll), no
//   imgui.ini.
// - Input: the game window's procedure is subclassed at the first Present (chained with
//   CallWindowProcW, so RSMods' subclass keeps working in either order). The hotkey's WM_KEYDOWN/UP
//   toggle and are not passed on. While visible, ImGui sees the window's messages and mouse/keyboard
//   messages it wants are not passed on, and the game's DirectInput mouse/keyboard input is hidden the
//   same way (overlay/capture.h, dinput_wrap.h).
// - Background driving: pipe commands "o ..." (overlay/overlaycmd.h), run at the next Present; the
//   overlay renders in an unfocused window too (WGC shots show it).

// [overlay] enabled, hotkey, open, scale: the overlay's own settings table (overlay_settings.cpp).
struct Rs7SetTable;
extern const Rs7SetTable kRs7OverlaySettings;
// Their section in the Window tab (platform/uilayout.h).
struct Rs7UiTable;
extern const Rs7UiTable kRs7OverlayUi;

// Startup worker, after Rs7Set_InitFromIni: reads [overlay] (enabled, hotkey, open), logs it.
void Rs7Overlay_Configure(const wchar_t* iniPath);
bool Rs7Overlay_Enabled();
// Worker: the game's device (d3d/worker.h Rs7WorkerOpts::onDevice).
void Rs7Overlay_OnDevice(void* device);
