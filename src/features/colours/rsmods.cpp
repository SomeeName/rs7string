// RSMods found as resource data of the installed XInput proxy, and the colour settings of its RSMods.ini.
#include "features/colours/state.h"
#include <windows.h>
#include <cstring>
#include <cwchar>
#include <string>

namespace rs7rsmods {
namespace {
using rs7colour::g_colour;

std::wstring ReadValue(const wchar_t* section, const wchar_t* key, const wchar_t* path) {
    wchar_t value[256] = {};
    const DWORD n = GetPrivateProfileStringW(section, key, L"", value, 256, path);

    return n < 255 ? std::wstring(value, n) : std::wstring(1, L'\x01');
}

bool IsRsmods(HMODULE module) {
    if (!module) {
        return false;
    }

    // RSMods' Version.rc identifies both InternalName and ProductName as RSMods.
    // The system XInput DLL (or an unrelated proxy with the same filename) must not opt us in.
    const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16));
    const DWORD size = resource ? SizeofResource(module, resource) : 0;
    const HGLOBAL data = resource ? LoadResource(module, resource) : nullptr;
    const auto bytes = data ? static_cast<const unsigned char*>(LockResource(data)) : nullptr;
    constexpr wchar_t marker[] = L"RSMods";
    if (!bytes) {
        return false;
    }

    for (size_t i = 0; i + sizeof(marker) <= size; i += sizeof(wchar_t)) {
        if (!std::memcmp(bytes + i, marker, sizeof(marker))) {
            return true;
        }
    }

    return false;
}
}  // namespace

Colour Read(const wchar_t* iniPath, bool loaded) {
    if (!loaded || !iniPath || !*iniPath) {
        return {};
    }
    if (_wcsicmp(ReadValue(L"Toggle Switches", L"ExtendedRange", iniPath).c_str(), L"on")) {
        return {};
    }

    const auto mode = ReadValue(L"Toggle Switches", L"CustomStringColors", iniPath);
    if (mode.empty() || mode == L"0" || mode == L"1") {
        // ExtendedRangeMode.hpp colorsNormal[0]: {0, 0.7764706, 0.5568628}.
        return {true, 0x00C68E};
    }
    if (mode != L"2") {
        return {};
    }

    const auto text = ReadValue(L"String Colors", L"string0_CB", iniPath);
    uint32_t rgb = 0;

    return Rs7_ParseRgb(text.c_str(), &rgb) ? Colour{true, rgb} : Colour{};
}

void Resolve(const Colour& colour, bool enabled, uint32_t (&rgb)[2]) {
    if (enabled && colour.valid) {
        rgb[0] = rgb[1] = colour.rgb;
    }
}

bool ReadCustomGlow(const wchar_t* iniPath) {
    if (!iniPath || !*iniPath) {
        return false;
    }

    // RSMods Settings: GetLongValue("CustomStringColors") == 2 (custom), IsOn(UseExactColors) is == "on".
    const auto mode = ReadValue(L"Toggle Switches", L"CustomStringColors", iniPath);
    const auto exact = ReadValue(L"Toggle Switches", L"UseExactColors", iniPath);

    return std::wcstol(mode.c_str(), nullptr, 10) == 2 && exact != L"on";
}

bool Identify(const wchar_t* proxyPath) {
    if (!proxyPath || !*proxyPath) {
        return false;
    }

    // Inspect the installed proxy as resource data. d3d9 loading does not mean
    // RSMods has loaded yet, and the system XInput DLL can share its basename.
    const HMODULE module =
        LoadLibraryExW(proxyPath, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    const bool identified = IsRsmods(module);

    if (module) {
        FreeLibrary(module);
    }

    return identified;
}

Colour ReadProxy(const wchar_t* proxyPath) {
    if (!proxyPath || !*proxyPath) {
        return {};
    }

    const std::wstring filename(proxyPath);
    const auto slash = filename.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }

    if (!Identify(proxyPath)) {
        return {};
    }

    const auto ini = filename.substr(0, slash + 1) + L"RSMods.ini";

    return Read(ini.c_str(), true);
}

void Init() {
    wchar_t path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);

    if (!n || n >= MAX_PATH) {
        g_colour.rsmods = {};
        g_colour.rsmodsPresent = g_colour.customGlow = false;
        return;
    }

    const std::wstring filename(path, n);
    const auto dir = filename.substr(0, filename.find_last_of(L"\\/") + 1);
    const auto proxy = dir + L"xinput1_3.dll";

    g_colour.rsmods = ReadProxy(proxy.c_str());
    g_colour.rsmodsPresent = Identify(proxy.c_str());
    g_colour.customGlow = g_colour.rsmodsPresent && ReadCustomGlow((dir + L"RSMods.ini").c_str());
}

bool Present() {
    return g_colour.rsmodsPresent;
}

bool CustomGlow() {
    return g_colour.customGlow;
}

const Colour& Cached() {
    return g_colour.rsmods;
}
}  // namespace rs7rsmods
