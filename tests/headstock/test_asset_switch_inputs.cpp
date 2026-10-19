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

TEST(asset_inputs_invalid_switch_does_not_enable_headstock) {
    for (const auto& value : std::vector<std::string>{"-1", "2", "1junk", std::string(1000, '9')}) {
        InputConfigure("[assets]\r\nheadstock7=" + value + "\r\n");
        CHECK_EQ(g_rs7_asset_on, uintptr_t(0));
        CHECK(InputLogged("invalid integer"));
    }
}
