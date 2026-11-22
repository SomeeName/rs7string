#include "features/menuload/menuload.h"
#include "features/menuload/queue.h"
#include "model/gamestring.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include "platform/seh.h"
#include <windows.h>

extern "C" __declspec(dllexport) uintptr_t g_rsxr_menu_request =
    0;  // MenuMovieRequest 0xC192A0, EAX = movie, stdcall(id lo, id hi)

namespace {
constexpr uint32_t kMovieVtable = 0x121E1B8;
constexpr uint32_t kMovieUri = 0x8C;    // game string: "urn:application:x-shockwave-flash:<name>"
constexpr uint32_t kMovieFlash = 0x58;  // the loaded Flash object; null until the request is answered

struct State {
    rsxrmenu::Queue queue;
    uint32_t requestMovie = 0;
    bool faulted = false;
};

State g_menu;

template <class T = uint32_t>
T Read(uint32_t base, size_t off = 0) {
    return Rsxr_SafeRead<T>(reinterpret_cast<const void*>(uintptr_t(base)), off);
}

uint64_t OwnerOf(uint32_t movie) {
    return Read<uint64_t>(Read(movie, 0x0C), 0x50);
}

bool ReadUri(uint32_t movie, char (&uri)[128]) {
    return rsxr::ReadGameString(reinterpret_cast<const void*>(uintptr_t(movie + kMovieUri)), uri, sizeof uri) && uri[0];
}

// A full call-in supplies the game's EAX argument without a new asm stub. The caller passes the two request-id
// dwords on the stack; the game returns directly to it with ret 8. Only the game thread enters this bridge.
void RequestEntry(RsxrMid::Context& c) {
    c.eax = g_menu.requestMovie;
    c.jump = uint32_t(g_rsxr_menu_request);
}

RsxrMid::CallIn g_requestEntry{&RequestEntry, RsxrMid::Mode::Full, 8};

void Request(uint32_t movie) {
    const uintptr_t entry = g_requestEntry.Entry();

    if (!entry || !g_rsxr_menu_request) {
        g_menu.faulted = true;
        RsxrLog("menu load: request bridge unavailable; fix off");
        return;
    }

    g_menu.requestMovie = movie;

    __try {
        reinterpret_cast<void(__stdcall*)(uint32_t, uint32_t)>(entry)(Read(movie, 0x40), Read(movie, 0x44));
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        g_menu.faulted = true;
        RsxrLog("menu load: movie request faulted (%08lX); fix off", GetExceptionCode());
    }
}

// Game thread, once per load-group update: send each rejected request whose file was indexed since, once. At most
// one pass over the queue per update; the clock is read again for each request because a resend re-enters the
// result hook, which stamps the time.
void Replay() {
    rsxrmenu::Why why{};

    for (size_t n = 0; n < sizeof g_menu.queue.items / sizeof g_menu.queue.items[0]; ++n) {
        auto* r = g_menu.queue.NextReplay(GetTickCount64(), why);

        if (!r) {
            return;
        }

        const uint32_t movie = r->movie;

        if (Read(movie) != kMovieVtable || OwnerOf(movie) != r->owner || Read(movie, kMovieFlash)) {
            g_menu.queue.Drop(*r);
            continue;
        }

        ++r->replays;

        if (why == rsxrmenu::Why::Published) {
            RsxrLog("menu load: '%s' is indexed now; asking again (request %u)", r->name, r->replays + 1);
        } else {
            RsxrLog("menu load: WARNING '%s' still rejected and its file was not seen being indexed; asking again "
                    "anyway (fallback %u of %u)",
                    r->name, r->fallbacks, rsxrmenu::kFallbackMax);
        }

        Request(movie);

        if (g_menu.faulted) {
            return;
        }
    }
}
}  // namespace

// 0xC167A0, before the movie request: track the request so a rejection can be answered later.
extern "C" __declspec(dllexport) void Rsxr_Mid_MenuMovieSetup(RsxrMid::Context& c) {
    char uri[128];

    if (g_rsxr_menu_retry && !g_menu.faulted && Read<uint8_t>(c.ecx, 0x121) && ReadUri(c.ecx, uri)) {
        g_menu.queue.Ask(c.ecx, OwnerOf(c.ecx), uri);
    }
}

// 0xC19335, after the locator call: AL = accepted. ESI = movie + 9Ch; an empty URI skips the call and lands here too.
extern "C" __declspec(dllexport) void Rsxr_Mid_MenuMovieResult(RsxrMid::Context& c) {
    if (!g_rsxr_menu_retry || g_menu.faulted) {
        return;
    }

    const uint32_t movie = c.esi - 0x9C;
    char uri[128];

    if (!ReadUri(movie, uri)) {
        return;
    }

    const bool accepted = (c.eax & 0xFF) != 0;
    const auto* r = g_menu.queue.Result(movie, accepted, GetTickCount64());

    if (!r) {
        return;
    }

    if (!accepted && !r->replays) {
        RsxrLog("menu load: '%s' was rejected (file not indexed yet); waiting for the indexer", r->name);
    } else if (!accepted) {
        RsxrLog("menu load: '%s' rejected again (request %u)", r->name, r->replays + 1);
    } else if (r->replays) {
        RsxrLog("menu load: '%s' accepted after %u requests", r->name, r->replays + 1);
    }
}

// 0x49DF80, indexing worker or loader, after a file is published and the index lock is released;
// [EBP-38h] = the file's name and extension. Only reads names and sets a flag; the game thread replays.
extern "C" __declspec(dllexport) void Rsxr_Mid_MenuIndexPublish(RsxrMid::Context& c) {
    if (!g_rsxr_menu_retry || g_menu.faulted || !g_menu.queue.active.load()) {
        return;
    }

    char file[128];

    if (rsxr::ReadGameString(reinterpret_cast<const void*>(uintptr_t(c.ebp - 0x38)), file, sizeof file)) {
        g_menu.queue.Publish(file);
    }
}

extern "C" __declspec(dllexport) void Rsxr_Mid_MenuLoadUpdate(RsxrMid::Context&) {
    if (g_rsxr_menu_retry && !g_menu.faulted) {
        Replay();
    }
}
