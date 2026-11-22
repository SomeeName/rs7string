// RSMods found as resource data of the installed XInput proxy (its version resource, or Audio Bridge's export), and
// the colour settings of its RSMods.ini.
#include "features/colours/state.h"
#include <windows.h>
#include <cstring>
#include <cwchar>
#include <string>

namespace rsxrrsmods {
namespace {
using rsxrcolour::g_colour;

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

// The named export of a module mapped by LoadLibraryEx as an image resource (sections at their RVAs).
bool HasExport(HMODULE module, const char* name) {
    if (!module) {
        return false;
    }

    // A data-file handle is the image base with its low bits set.
    const auto base = reinterpret_cast<const unsigned char*>(reinterpret_cast<uintptr_t>(module) & ~uintptr_t(3));
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) {
        return false;
    }

    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress || dir.VirtualAddress >= nt->OptionalHeader.SizeOfImage) {
        return false;
    }

    const DWORD size = nt->OptionalHeader.SizeOfImage;
    if (size - dir.VirtualAddress < sizeof(IMAGE_EXPORT_DIRECTORY)) {
        return false;
    }

    const auto exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    if (exports->AddressOfNames >= size || exports->NumberOfNames > (size - exports->AddressOfNames) / sizeof(DWORD)) {
        return false;
    }

    const auto names = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
    const size_t length = std::strlen(name) + 1;

    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        if (names[i] < size && size - names[i] >= length &&
            !std::memcmp(reinterpret_cast<const char*>(base + names[i]), name, length)) {
            return true;
        }
    }

    return false;
}

// Rocksmith Audio Bridge, an RSMods build without a version resource, exports this next to the XInput functions.
constexpr char kAudioBridgeExport[] = "RSMods_BridgeDriverLog";

HMODULE MapProxy(const wchar_t* proxyPath) {
    return LoadLibraryExW(proxyPath, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
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

    return Rsxr_ParseRgb(text.c_str(), &rgb) ? Colour{true, rgb} : Colour{};
}

void Resolve(const Colour& colour, bool enabled, uint32_t (&rgb)[2]) {
    if (enabled && colour.valid) {
        rgb[0] = rgb[1] = colour.rgb;
    }
}

bool ReadCustomGlow(const wchar_t* iniPath, bool exactColours) {
    if (!iniPath || !*iniPath) {
        return false;
    }

    // RSMods Settings: GetLongValue("CustomStringColors") == 2 (custom), IsOn(UseExactColors) is == "on"
    // (compared case-insensitively, like ExtendedRange in Read).
    const auto mode = ReadValue(L"Toggle Switches", L"CustomStringColors", iniPath);
    const auto exact = ReadValue(L"Toggle Switches", L"UseExactColors", iniPath);

    return std::wcstol(mode.c_str(), nullptr, 10) == 2 && (!exactColours || _wcsicmp(exact.c_str(), L"on") != 0);
}

bool Identify(const wchar_t* proxyPath) {
    if (!proxyPath || !*proxyPath) {
        return false;
    }

    // Inspect the installed proxy as resource data. d3d9 loading does not mean
    // RSMods has loaded yet, and the system XInput DLL can share its basename.
    const HMODULE module = MapProxy(proxyPath);
    const bool identified = IsRsmods(module) || HasExport(module, kAudioBridgeExport);

    if (module) {
        FreeLibrary(module);
    }

    return identified;
}

bool ProxyExports(const wchar_t* path, const char* name) {
    if (!path || !*path || !name) {
        return false;
    }

    const HMODULE module = MapProxy(path);
    const bool found = HasExport(module, name);

    if (module) {
        FreeLibrary(module);
    }

    return found;
}

bool IsAudioBridge(const wchar_t* proxyPath) {
    return ProxyExports(proxyPath, kAudioBridgeExport);
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
    const auto ini = dir + L"RSMods.ini";
    // Map the proxy once: Identify + IsAudioBridge in one pass (ReadProxy would map it twice more).
    const HMODULE module = MapProxy((dir + L"xinput1_3.dll").c_str());
    const bool bridge = HasExport(module, kAudioBridgeExport);
    const bool present = bridge || IsRsmods(module);

    if (module) {
        FreeLibrary(module);
    }

    g_colour.rsmods = present ? Read(ini.c_str(), true) : Colour{};
    g_colour.rsmodsPresent = present;
    // Audio Bridge is built on an RSMods without UseExactColors, so that key does not apply to it.
    g_colour.customGlow = present && ReadCustomGlow(ini.c_str(), !bridge);
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
}  // namespace rsxrrsmods
