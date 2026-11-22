// The song-end log's mid-hooks SE1-SE3 (songend.h).
#include "features/allocstats/allocstats.h"
#include "features/songend/songend.h"
#include "platform/midhook.h"
#include "platform/safecopy.h"

namespace {
void SongEnd(uint32_t site, RsxrMid::Context& context) {
    // CONFIRMED: the old SE1/SE2/SE3 stubs CMP this setting before PUSHFD.
    // Preserve even their dead arithmetic flags; setting values are dwords.
    const uint32_t setting = uint32_t(g_rsxr_endlog);

    context.Compare32(setting, 0);
    if (setting) {
        RsxrEnd_OnContext(site, context);
    }
}
}  // namespace

extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_SE1(RsxrMid::Context& context) {
    SongEnd(1, context);
}

extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_SE2(RsxrMid::Context& context) {
    uint16_t old = 0;

    if (Rsxr_SafeCopy(&old, reinterpret_cast<const void*>(uintptr_t(context.esp) + 4), sizeof old) && old == 3) {
        RsxrAlloc_Snapshot("song end");  // controlled by alloc_stats independently of song_end_log
    }
    SongEnd(2, context);
}

extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_SE3(RsxrMid::Context& context) {
    SongEnd(3, context);
}
