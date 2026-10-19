#include "features/headstock/assetload.h"
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
