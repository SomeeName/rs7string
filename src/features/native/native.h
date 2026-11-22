#pragma once
#include "platform/hooktable.h"
#include <cstdint>

// Native N-string charts (docs/NATIVE-FORMAT.md). The game and the rest of the mod consume the internal form (a
// stock six-string chart plus the extension block and the RsxrString manifest keys), so three hooks translate a native
// package as the game loads it:
//   PN1  the package: the PSARC magic "PSXR" is accepted where a stock game requires "PSAR"
//   JM1  the manifest and header files: native Tuning objects are rewritten before the game's JSON parser reads them
//   XI1  the chart: an SNXR body is converted in place before the game's first section parser reads it
extern const RsxrHookTable kRsxrNativeHooks;

namespace rsxrnative {
// "PSAR" and "PSXR" as the loader compares them (the header's first four bytes, read big-endian).
constexpr uint32_t kStockMagic = 0x50534152, kPackageMagic = 0x50535852;
}  // namespace rsxrnative

extern "C" {
// XI1's work: the body buffer of an SNG being loaded. False: the body is SNXR and could not be converted.
bool __cdecl RsxrNative_OnBody(uintptr_t buffer, uint32_t size);
// JM1's work: *cursor is the JSON text about to be parsed. A text with native arrangements is rewritten into a
// buffer of the calling thread and *cursor moved there; the buffer stays valid until that thread's next rewrite.
void __cdecl RsxrNative_OnJson(const char** cursor);
}
