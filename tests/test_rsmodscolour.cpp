#include "core/rsmodscolour.h"
#include "test.h"
#include <windows.h>
#include <cstdio>
#include <string>

namespace {
std::wstring TempIni(const char* content) {
    wchar_t dir[MAX_PATH] = {}, path[MAX_PATH] = {};

    GetTempPathW(MAX_PATH, dir);
    GetTempFileNameW(dir, L"r7c", 0, path);
    FILE* f = nullptr;

    _wfopen_s(&f, path, L"wb");

    if (f) {
        std::fputs(content, f);
        std::fclose(f);
    }

    return path;
}
}  // namespace

TEST(rsmods_colour_requires_loaded_mod_enabled_er_and_opt_in) {
    const auto p = TempIni("[Toggle Switches]\r\nExtendedRange=on\r\nCustomStringColors=0\r\n");
    const auto absent = rs7rsmods::Read(p.c_str(), false);
    const auto present = rs7rsmods::Read(p.c_str(), true);

    CHECK(!absent.valid);
    CHECK(present.valid);
    CHECK_EQ(present.rgb, 0x00C68Eu);
    uint32_t rgb[2] = {0x112233u, 0x445566u};

    rs7rsmods::Resolve(present, false, rgb);
    CHECK_EQ(rgb[0], 0x112233u);
    CHECK_EQ(rgb[1], 0x445566u);
    rs7rsmods::Resolve(present, true, rgb);
    CHECK_EQ(rgb[0], 0x00C68Eu);
    CHECK_EQ(rgb[1], 0x00C68Eu);
    DeleteFileW(p.c_str());
}

TEST(rsmods_colour_reads_custom_low_b_for_both_profile_modes) {
    const auto p = TempIni("[Toggle Switches]\r\nExtendedRange=on\r\nCustomStringColors=2\r\n[String "
                           "Colors]\r\nstring0=ABCDEF\r\nstring0_CB=1234AB\r\n");
    const auto policy = rs7rsmods::Read(p.c_str(), true);

    CHECK(policy.valid);
    uint32_t rgb[2] = {0xC8C8C8u, 0xC8C8C8u};

    rs7rsmods::Resolve(policy, true, rgb);
    CHECK_EQ(rgb[0], 0x1234ABu);
    CHECK_EQ(rgb[1], 0x1234ABu);
    DeleteFileW(p.c_str());
}

TEST(rsmods_colour_invalid_or_disabled_configuration_preserves_own_palette) {
    const char* cases[] = {"",
                           "[Toggle Switches]\r\nExtendedRange=off\r\n",
                           "[Toggle Switches]\r\nExtendedRange=on\r\nCustomStringColors=3\r\n",
                           "[Toggle Switches]\r\nExtendedRange=on\r\nCustomStringColors=garbage\r\n",
                           "[Toggle Switches]\r\nExtendedRange=on\r\nCustomStringColors=2\r\n",
                           "[Toggle Switches]\r\nExtendedRange=on\r\nCustomStringColors=2\r\n[String "
                           "Colors]\r\nstring0_CB=123456junk\r\n"};

    for (const auto content : cases) {
        const auto p = TempIni(content);
        const auto policy = rs7rsmods::Read(p.c_str(), true);

        CHECK(!policy.valid);
        uint32_t rgb[2] = {0x112233u, 0x445566u};

        rs7rsmods::Resolve(policy, true, rgb);
        CHECK_EQ(rgb[0], 0x112233u);
        CHECK_EQ(rgb[1], 0x445566u);
        DeleteFileW(p.c_str());
    }

    CHECK(!rs7rsmods::Read(L"Z:\\missing\\RSMods.ini", true).valid);
}

TEST(rsmods_colour_standard_alternative_uses_same_low_b) {
    const auto p = TempIni(
        "[Toggle Switches]\r\nExtendedRange=on\r\nCustomStringColors=1\r\n[String Colors]\r\nstring0_CB=123456\r\n");
    const auto policy = rs7rsmods::Read(p.c_str(), true);

    CHECK(policy.valid);
    CHECK_EQ(policy.rgb, 0x00C68Eu);
    DeleteFileW(p.c_str());
}

TEST(rsmods_colour_truncated_custom_value_is_invalid) {
    const std::string content =
        "[Toggle Switches]\r\nExtendedRange=on\r\nCustomStringColors=2\r\n[String Colors]\r\nstring0_CB=123456" +
        std::string(300, ' ') + "junk\r\n";
    const auto p = TempIni(content.c_str());

    CHECK(!rs7rsmods::Read(p.c_str(), true).valid);
    DeleteFileW(p.c_str());
}

TEST(rsmods_colour_detects_installed_proxy_before_it_is_loaded) {
    const auto dir = TempIni("");

    DeleteFileW(dir.c_str());
    CHECK(CreateDirectoryW(dir.c_str(), nullptr));
    const auto proxy = dir + L"\\xinput1_3.dll";
    const auto ini = dir + L"\\RSMods.ini";
    wchar_t exe[MAX_PATH] = {};

    CHECK(GetModuleFileNameW(nullptr, exe, MAX_PATH) != 0);
    // The test executable has only a synthetic RSMods version resource. Copy it
    // under the proxy filename; detection must inspect it without loading code.
    CHECK(CopyFileW(exe, proxy.c_str(), TRUE));
    CHECK(WritePrivateProfileStringW(L"Toggle Switches", L"ExtendedRange", L"on", ini.c_str()));
    CHECK(WritePrivateProfileStringW(L"Toggle Switches", L"CustomStringColors", L"2", ini.c_str()));
    CHECK(WritePrivateProfileStringW(L"String Colors", L"string0_CB", L"00c68e", ini.c_str()));
    CHECK(GetModuleHandleW(proxy.c_str()) == nullptr);
    const auto colour = rs7rsmods::ReadProxy(proxy.c_str());

    CHECK(colour.valid);
    CHECK_EQ(colour.rgb, 0x00C68Eu);
    CHECK(GetModuleHandleW(proxy.c_str()) == nullptr);
    // A second read also works, and no data-file handle pins the proxy on disk.
    CHECK(rs7rsmods::ReadProxy(proxy.c_str()).valid);
    CHECK(DeleteFileW(proxy.c_str()));
    CHECK(DeleteFileW(ini.c_str()));
    CHECK(RemoveDirectoryW(dir.c_str()));
}

TEST(rsmods_colour_proxy_rejects_missing_or_unidentified_files) {
    CHECK(!rs7rsmods::ReadProxy(nullptr).valid);
    CHECK(!rs7rsmods::ReadProxy(L"").valid);
    CHECK(!rs7rsmods::ReadProxy(L"Z:\\missing\\xinput1_3.dll").valid);
    const auto file = TempIni("RSMods");  // Text with the marker is not an identified PE proxy.

    CHECK(!rs7rsmods::ReadProxy(file.c_str()).valid);
    CHECK(DeleteFileW(file.c_str()));
}
