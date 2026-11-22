// The three hooks that translate a native package (native.h) while the game loads it. Everything after them (the
// game, XB1, the N-slot chord data, the manifest readers of the tuner and C-lite) sees the current form only.
#include "features/native/native.h"
#include "features/tones/tones.h"
#include "model/nativemanifest.h"
#include "model/nativesng.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include "platform/safecopy.h"
#include <windows.h>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
__declspec(dllexport) uintptr_t g_rsxr_resume_XI1 = 0;
}

namespace {

constexpr uint32_t kXi1Length = 7;
constexpr size_t kMaxBody = 64u << 20;
constexpr size_t kMaxJson = 16u << 20;
volatile LONG g_bodyLogged = 0, g_jsonLogged = 0, g_packageLogged = 0;

enum class Result { NotNative, Converted, Failed };

Result ConvertInPlace(uintptr_t buffer, size_t size) {
    uint8_t head[4]{};

    if (!buffer || size < sizeof head || !Rsxr_SafeCopy(head, reinterpret_cast<const void*>(buffer), sizeof head) ||
        !rsxrnative::IsNative(head, sizeof head)) {
        return Result::NotNative;
    }

    const bool log = RsxrLog_Upto(&g_bodyLogged, 200);

    if (size > kMaxBody) {
        if (log) {
            RsxrLog("native: SNXR body of %zu bytes is too large: load refused", size);
        }

        return Result::Failed;
    }

    std::vector<uint8_t> body(size);
    if (!Rsxr_SafeCopy(body.data(), reinterpret_cast<const void*>(buffer), size)) {
        return Result::Failed;
    }

    const unsigned instrument = size > 6 ? body[5] : 0, strings = size > 6 ? body[6] : 0;
    char why[200] = "";
    const rsxrnative::Status st = rsxrnative::Convert(body.data(), size, &body, why, sizeof why);

    if (st != rsxrnative::Status::Ok || body.size() != size ||
        !Rsxr_SafeStore(reinterpret_cast<void*>(buffer), body.data(), size)) {
        if (log) {
            RsxrLog("native: SNXR body (%zu bytes) not converted (status %d: %s): load refused", size, int(st), why);
        }

        return Result::Failed;
    }

    if (log) {
        RsxrLog("native: SNXR body converted in place: %s, %u strings, %zu bytes", instrument ? "bass" : "guitar",
                strings, size);
    }

    return Result::Converted;
}

void RewriteJson(const char** cursor) {
    thread_local std::string buffer;
    const char* text = *cursor;

    if (!text) {
        return;
    }

    // The value's own extent: the buffer is not always zero-terminated right behind the closing bracket.
    const size_t size = rsxrnative::JsonExtent(text, kMaxJson);
    rsxrnative::ManifestStats stats;

    if (!size || !rsxrnative::RewriteManifest(text, size, &buffer, &stats)) {
        return;
    }

    *cursor = buffer.c_str();

    if (RsxrLog_Upto(&g_jsonLogged, 200)) {
        RsxrLog("native: manifest text of %zu bytes: %d native arrangement(s) rewritten to the current form%s", size,
                stats.rewritten, stats.bad ? " - INVALID native arrangement(s) made six-string" : "");
    }
}

}  // namespace

extern "C" bool __cdecl RsxrNative_OnBody(uintptr_t buffer, uint32_t size) {
    try {
        return ConvertInPlace(buffer, size) != Result::Failed;
    } catch (...) {
        return false;  // allocation failure: the stock parser must not run over an SNXR body
    }
}

extern "C" void __cdecl RsxrNative_OnJson(const char** cursor) {
    try {
        if (cursor) {
            RewriteJson(cursor);
        }
    } catch (...) {
        // Allocation failure: the game parses the original text.
    }
}

// XI1 - 0x558DD6 in SNG_Song_Load 0x558BD0 (loader thread), after the inflate checks: the body stream is at the
// site's ESP + 14h (+10h buffer, +18h size). The thunk replays `lea edx,[esp+14h]; lea eax,[ebx+34h]`. A body that
// cannot be converted continues at the loader's own failure exit instead (the target of the `jne rel32` that ends 3
// bytes before the site; it destroys both streams and returns false), with the site's stack. Must not throw.
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_XI1(RsxrMid::Context& c) {
    const uint32_t buffer = RsxrMid::Dword(c.esp + 0x24);
    const uint32_t size = RsxrMid::Dword(c.esp + 0x2C);
    const bool native =
        size >= 4 && Rsxr_SafeRead<uint32_t>(reinterpret_cast<const void*>(uintptr_t(buffer)), 0) == 0x52584E53u;
    const bool accepted = RsxrNative_OnBody(buffer, size);

    RsxrTones_SongLoaded(c.ebx, accepted && native);

    if (accepted) {
        return;
    }

    const uint32_t site = static_cast<uint32_t>(g_rsxr_resume_XI1) - kXi1Length;

    c.jump = site - 3 + RsxrMid::Dword(site - 7);
}

// JM1 - 0x889010, the entry of the game's JSON parser (cdecl: output, 0, pointer to the text cursor; no length, the
// text is zero-terminated). All its callers pass through here, the manifest cache (0x4A8BD6) and the asset factory
// (0x87B8CD) among them; neither uses the cursor after the call, and both free their own buffer, not the cursor.
// The rewritten text lives in one buffer per thread until that thread's next rewrite: the entry is not re-entered
// while it runs (nested values go through the value parsers, not through here).
// The thunk replays `push ebp; mov ebp,esp; and esp,-8`. Must not throw.
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_JM1(RsxrMid::Context& c) {
    RsxrNative_OnJson(RsxrMid::Ptr<const char*>(RsxrMid::Dword(c.esp + 0xC)));
}

// PN1 - 0x8A34D0 in the PSARC open function 0x8A3440: `cmp eax,"PSAR"` on the header's magic, which the function
// has just stored byte-swapped at [EBP-70h] (the header is copied from there into the table of contents buffer).
// A native package's magic becomes the stock one in both places; the thunk replays the compare. A game without
// this hook fails the compare and drops the package before anything is allocated.
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_PN1(RsxrMid::Context& c) {
    if (c.eax != rsxrnative::kPackageMagic) {
        return;
    }

    c.eax = rsxrnative::kStockMagic;
    *RsxrMid::Ptr<uint32_t>(c.ebp - 0x70) = rsxrnative::kStockMagic;

    if (RsxrLog_Upto(&g_packageLogged, 50)) {
        RsxrLog("native: package with the PSXR magic accepted");
    }
}
