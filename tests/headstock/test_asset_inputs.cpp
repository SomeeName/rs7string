#include "features/headstock/assetload.h"
#include "features/headstock/assetload_config.h"
#include "features/headstock/state.h"
#include "platform/log.h"
#include "test.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
std::vector<std::string> inputLogs;

void InputLog(const char* s) {
    inputLogs.emplace_back(s);
}

std::filesystem::path InputIni(const std::string& text) {
    wchar_t temp[MAX_PATH]{};

    GetTempPathW(MAX_PATH, temp);
    const auto p = std::filesystem::path(temp) / ("rs7_asset_inputs_" + std::to_string(GetCurrentProcessId()) + ".ini");
    std::ofstream out(p, std::ios::binary);

    out << text;

    return p;
}

bool InputLogged(const std::string& part) {
    for (const auto& s : inputLogs) {
        if (s.find(part) != std::string::npos) {
            return true;
        }
    }

    return false;
}

void InputConfigure(const std::string& text) {
    const auto p = InputIni(text);

    inputLogs.clear();
    Rs7Log_SetSink(InputLog);
    Rs7Asset_Configure(p.c_str());
    Rs7Log_SetSink(nullptr);
    std::filesystem::remove(p);
}
}  // namespace

TEST(asset_inputs_truncated_ini_strings_disable_headstock) {
    for (const char* key : {"dir", "headstock7_file", "headstock7_file_lh", "suffix"}) {
        InputConfigure(std::string("[assets]\r\n") + key + "=" + std::string(400, 'a') + "\r\n");
        CHECK_EQ(g_rs7_asset_on, uintptr_t(0));
        CHECK(InputLogged("truncated"));
    }

    InputConfigure("[assets]\r\nheadstock7=0\r\n");
}

TEST(asset_inputs_wood_rejects_trailing_numeric_junk) {
    for (const char* value : {"0.8oops", "0.8  garbage", "nan", "inf"}) {
        InputConfigure(std::string("[assets]\r\nheadstock7=0\r\nheadstock7_wood=") + value + "\r\n");
        CHECK(InputLogged("headstock7_wood=0.60"));
    }

    InputConfigure("[assets]\r\nheadstock7=0\r\nheadstock7_wood=0.8   \r\n");
    CHECK(InputLogged("headstock7_wood=0.80"));
}

TEST(asset_inputs_explicit_model_paths_are_protected_even_when_missing) {
    InputConfigure("[assets]\r\nheadstock8_file=custom.nif\r\nheadstock7_file=seven.nif\r\n"
                   "headstock8_file_lh=left.nif\r\n");
    CHECK(!rs7head::g_head.sbWanted && !rs7head::g_head.sbWantedLh);
    CHECK_EQ(rs7asset::g_config.sbProtected.size(), 3u);
    InputConfigure("[assets]\r\nheadstock7=0\r\n");
    CHECK(rs7asset::g_config.sbProtected.empty());
}

TEST(asset_inputs_default_filenames_remain_managed_and_opt_out_still_disables_builds) {
    InputConfigure("[assets]\r\nheadstock8_file=.\\RS7_INLINE8.NIF\r\n"
                   "headstock8_file_lh=.\\rs7_inline8_lh.nif\r\n"
                   "headstock7_file=.\\rs7_inline7.nif\r\nheadstock7_file_lh=.\\rs7_inline7_lh.nif\r\n");
    CHECK(rs7head::g_head.sbWanted && rs7head::g_head.sbWantedLh);
    CHECK(rs7asset::g_config.sbProtected.empty());

    InputConfigure("[assets]\r\nheadstock8_file=custom\\rs7_inline8.nif\r\n");
    CHECK(!rs7head::g_head.sbWanted && rs7head::g_head.sbWantedLh);
    CHECK_EQ(rs7asset::g_config.sbProtected.size(), 1u);

    for (const char* value : {"", "none", "off", "0"}) {
        InputConfigure(std::string("[assets]\r\nheadstock8_file=") + value + "\r\nheadstock8_file_lh=" + value +
                       "\r\n");
        CHECK(!rs7head::g_head.sbWanted && !rs7head::g_head.sbWantedLh);
        CHECK(rs7asset::g_config.sbProtected.empty());
    }

    InputConfigure("[assets]\r\nheadstock7=0\r\n");
}
