#include "core/rsmodscolour.h"
#include "core/color.h"
#include <windows.h>
#include <cstring>
#include <cwchar>
#include <string>

namespace rs7rsmods {
namespace {
Colour g_cached;

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
    const auto bytes =
        resource ? static_cast<const unsigned char*>(LockResource(LoadResource(module, resource))) : nullptr;
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

Colour ReadProxy(const wchar_t* proxyPath) {
    if (!proxyPath || !*proxyPath) {
        return {};
    }

    const std::wstring filename(proxyPath);
    const auto slash = filename.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }

    // Inspect the installed proxy as resource data. d3d9 loading does not mean
    // RSMods has loaded yet, and the system XInput DLL can share its basename.
    const HMODULE module =
        LoadLibraryExW(proxyPath, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    const bool identified = IsRsmods(module);

    if (module) {
        FreeLibrary(module);
    }
    if (!identified) {
        return {};
    }

    const auto ini = filename.substr(0, slash + 1) + L"RSMods.ini";

    return Read(ini.c_str(), true);
}

void Init() {
    wchar_t path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);

    if (!n || n >= MAX_PATH) {
        g_cached = {};
        return;
    }

    const std::wstring filename(path, n);
    const auto proxy = filename.substr(0, filename.find_last_of(L"\\/") + 1) + L"xinput1_3.dll";

    g_cached = ReadProxy(proxy.c_str());
}

const Colour& Cached() {
    return g_cached;
}
}  // namespace rs7rsmods
