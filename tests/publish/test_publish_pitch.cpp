#include "features/diagrams/diagrams.h"
#include "features/nslot/nslot.h"
#include "features/publish/publish.h"
#include "test.h"
#include <cstring>

TEST(publish_t8_eighth_column_without_diagrams) {
    const auto oldPorts = g_rs7_publish_ports, oldDiagrams = g_rs7_diagrams;
    uint8_t tpl[rs7ns::kTplSize] = {}, out[rs7ns::kT8Size] = {};

    std::memset(tpl + 4, 0xff, 12);
    std::memset(tpl + 0x10, 0xff, 24);
    const rs7ns::Col extra[2] = {{4, 1}, {5, 2}};
    const int16_t tuning[8] = {0, 0, 0, 0, 0, 0, -2, -3};

    g_rs7_diagrams = 0;
    g_rs7_publish_ports = 0;
    rs7ns::BuildT8(tpl, extra, tuning, 8, 0, out);
    CHECK_EQ(reinterpret_cast<const int32_t*>(out + rs7ns::kT8Midi)[6], -1);
    CHECK_EQ(reinterpret_cast<const int32_t*>(out + rs7ns::kT8Midi)[7], -1);
    g_rs7_publish_ports = 1;
    rs7ns::BuildT8(tpl, extra, tuning, 8, 0, out);
    CHECK_EQ(reinterpret_cast<const int32_t*>(out + rs7ns::kT8Midi)[6], 35 - 2 + 4);
    CHECK_EQ(reinterpret_cast<const int32_t*>(out + rs7ns::kT8Midi)[7], 30 - 3 + 5);
    CHECK_EQ(out[rs7ns::kT8Used], 0xc0);
    g_rs7_publish_ports = oldPorts;
    g_rs7_diagrams = oldDiagrams;
}
