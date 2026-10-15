#pragma once
// Dear ImGui build options for rs7string's overlay (IMGUI_USER_CONFIG, rs7string.vcxproj). The vendored
// third_party/imgui files are upstream v1.92.9b, unmodified.

#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
// No imgui.ini (RSMods' own ImGui writes one into the game folder) and no file logging.
#define IMGUI_DISABLE_FILE_FUNCTIONS
// No imm32.dll / shell32.dll imports (IME position, open-URL helpers).
#define IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS
#define IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
// No XInput: the win32 backend would LoadLibrary xinput1_4/1_3 (xinput1_3.dll is RSMods in the game folder).
#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#define IMGUI_DISABLE_DEBUG_TOOLS
