#include "features/testdrive/testdrive.h"
#include "platform/config.h"
#include "platform/log.h"
#include "platform/patch.h"
#include "platform/resolve.h"
#include "platform/retry.h"
#include <windows.h>
#include <cstring>

void Rs7Fast_Start(const wchar_t* iniPath, const Rs7Site* sites, int count, uintptr_t textVa, size_t textSize) {
    if (!Rs7Config_ReadInt(L"test", L"fast", 0, iniPath)) {
        Rs7Log("config: [test] fast=0 (off)");
        return;
    }

    Rs7ResolveResult res;

    Rs7_Retry(20, 100, [&] {  // .text may still be unpacking
        res = Rs7_ResolveAll(sites, count, reinterpret_cast<const uint8_t*>(textVa), textSize, textVa);

        return res.ok;
    });

    for (const auto& e : res.errors) {
        Rs7Log("fast: site %s - left alone", e.c_str());
    }

    for (const auto& r : res.resolved) {
        if (!std::strcmp(r.site->id, "FST_REVIEW")) {
            static const uint8_t kNop2[] = {0x90, 0x90};
            const bool ok = Rs7_CasCode(r.addr, r.original.data(), kNop2, sizeof kNop2) == Rs7CasResult::Written;

            Rs7Log("fast: after a song -> LearnASong_SongOptions (results screen skipped)%s",
                   ok ? "" : " - NOT written");
        }
    }
}
