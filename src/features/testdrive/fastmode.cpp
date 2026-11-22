#include "features/testdrive/testdrive.h"
#include "platform/config.h"
#include "platform/log.h"
#include "platform/patch.h"
#include "platform/resolve.h"
#include "platform/retry.h"
#include <windows.h>
#include <cstring>

void RsxrFast_Start(const wchar_t* iniPath, const RsxrSite* sites, int count, uintptr_t textVa, size_t textSize) {
    if (!RsxrConfig_ReadInt(L"test", L"fast", 0, iniPath)) {
        RsxrLog("config: [test] fast=0 (off)");
        return;
    }

    RsxrResolveResult res;

    Rsxr_Retry(20, 100, [&] {  // .text may still be unpacking
        res = Rsxr_ResolveAll(sites, count, reinterpret_cast<const uint8_t*>(textVa), textSize, textVa);

        return res.ok;
    });

    for (const auto& e : res.errors) {
        RsxrLog("fast: site %s - left alone", e.c_str());
    }

    for (const auto& r : res.resolved) {
        if (!std::strcmp(r.site->id, "FST_REVIEW")) {
            static const uint8_t kNop2[] = {0x90, 0x90};
            const RsxrCasResult result = Rsxr_CasCode(r.addr, r.original.data(), kNop2, sizeof kNop2);
            const char* status =
                result == RsxrCasResult::Written                 ? ""
                : result == RsxrCasResult::WrittenCleanupFailed  ? " - written, protection restore FAILED"
                : result == RsxrCasResult::MismatchCleanupFailed ? " - NOT written, protection restore FAILED"
                                                                 : " - NOT written";

            RsxrLog("fast: after a song -> LearnASong_SongOptions (results screen skipped)%s", status);
        }
    }
}
