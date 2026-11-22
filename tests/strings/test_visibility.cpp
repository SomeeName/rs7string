#include "executable.h"
#include "features/publish/publish.h"
#include "features/string7/string7.h"
#include "features/strings/strings.h"
#include "test.h"
#include "testutil.h"

namespace {
// An Entity: its visibility flag pointer sits at +4.
struct Entity {
    uint32_t head;
    uint32_t* visible;
};
}  // namespace

TEST(strings_native_line_setting_hides_extras_without_changing_counts) {
    rsxrtest::Restore ports(g_rsxr_publish_ports), publish(g_rsxr_native_publish), native(g_rsxr_native_strings);
    rsxrtest::Restore setting(g_rsxr_s7_on), show(g_rsxr_fn_show), detach(g_rsxr_fn_detach);
    // mov ecx,[esp+4]; mov [eax],ecx; ret (Entity_Show: EAX entity, cdecl show).
    rsxrtest::Executable showCode{0x8B, 0x4C, 0x24, 0x04, 0x89, 0x08, 0xC3};

    g_rsxr_publish_ports = g_rsxr_native_publish = g_rsxr_native_strings = 1;
    g_rsxr_fn_show = uintptr_t(showCode.p);
    g_rsxr_fn_detach = 0;

    for (const uint8_t kind : {uint8_t(1), uint8_t(2)}) {
        const int stock = kind == 2 ? 4 : 6;

        for (int n = stock + 1; n <= stock + 2; ++n) {
            uint8_t info[rsxrgs::kInfoSize] = {}, gs[rsxrgs::kGsSize] = {};
            Entity objects[8] = {}, ghosts[8] = {};
            uint32_t visible[8] = {}, ghostVisible[8] = {};

            RsxrGs_InfoAllocated(info);
            info[rsxr::kInfoKind] = kind;
            info[rsxr::kInfoStrings] = uint8_t(n);

            for (int s = 0; s < 8; ++s) {
                objects[s].visible = &visible[s];
                ghosts[s].visible = &ghostVisible[s];

                if (s < 6) {
                    *reinterpret_cast<uint8_t**>(gs + 4 + 4 * s) = reinterpret_cast<uint8_t*>(&objects[s]);
                    *reinterpret_cast<uint8_t**>(gs + 0x1C + 4 * s) = reinterpret_cast<uint8_t*>(&ghosts[s]);
                }

                visible[s] = ghostVisible[s] = 1;
            }

            uint8_t *main[2] = {reinterpret_cast<uint8_t*>(&objects[6]), reinterpret_cast<uint8_t*>(&objects[7])},
                    *ghost[2] = {reinterpret_cast<uint8_t*>(&ghosts[6]), reinterpret_cast<uint8_t*>(&ghosts[7])};

            rsxrgs::InitTail(gs, nullptr, info, main, ghost);

            for (const uintptr_t on : {uintptr_t(0), uintptr_t(1), uintptr_t(0)}) {
                g_rsxr_s7_on = on;
                CHECK(RsxrGs_Frame(nullptr, gs, true));
                CHECK_EQ(info[rsxr::kInfoStrings], n);
                CHECK_EQ(info[rsxrgs::kInfoActive], n);
                CHECK_EQ(info[rsxrgs::kInfoLoaded], kind == 2 ? n : 8);

                for (int s = stock; s < n; ++s) {
                    CHECK_EQ(visible[s], uint32_t(on));
                    if (!on) {
                        CHECK_EQ(ghostVisible[s], 0u);
                    }
                }
            }

            CHECK_EQ(RsxrGs_InfoFreeSize(info), rsxrgs::kInfoSize);
        }
    }
}
