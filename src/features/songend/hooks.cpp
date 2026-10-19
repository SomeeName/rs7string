// The song-end log's mid-hooks SE1-SE3 (songend.h).
#include "features/songend/songend.h"
#include "platform/midhook.h"

namespace {
void SongEnd(uint32_t site, Rs7Mid::Context& context) {
    // CONFIRMED: the old SE1/SE2/SE3 stubs CMP this setting before PUSHFD.
    // Preserve even their dead arithmetic flags; setting values are dwords.
    const uint32_t setting = uint32_t(g_rs7_endlog);

    context.Compare32(setting, 0);
    if (setting) {
        Rs7End_OnContext(site, context);
    }
}
}  // namespace

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_SE1(Rs7Mid::Context& context) {
    SongEnd(1, context);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_SE2(Rs7Mid::Context& context) {
    SongEnd(2, context);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_SE3(Rs7Mid::Context& context) {
    SongEnd(3, context);
}
