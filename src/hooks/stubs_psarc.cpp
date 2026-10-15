// Stubs of the plain-PSARC unit (core/plainpsarc.h, sites.json group "psarc"). Same behaviour as the
// plain-PSARC enabler build (RSCDLCEnabler-TooManyCoresFix PR #1, PlainPsarc.cpp); exported for
// tools/tests/test_emu_psarc.py.
#include "core/plainpsarc.h"
#include "hooks/stub_macros.h"
#include <cstdint>
#include <cstring>

//   PP_TOC   0x8A3643 PSARC TOC loader: plain TOC (archive flag bit 4 clear) skips the decrypt block
//   PP_SNGF  0x558C1A SNG loader: flags 1 (plain) passes the flags check
//   PP_SNGD  0x558CD4 SNG loader: the call of the SNG decrypt becomes decrypt-or-copy (a C function)
RS7_RESUME_VAR(PP_TOC);      // site + 5: mov esi,<TOC key> (the stock decrypt path)
RS7_VAR(g_rs7_skip_PP_TOC);  // site + 0x4D: lea ecx,[edi+34h], after the decrypt block
RS7_RESUME_VAR(PP_SNGF);     // site + 5: the stock jne <reject>
RS7_TARGET_VAR(PP_SNGD);     // the game's SNG decrypt (0xE3EA10)

// PP_TOC - replaces mov ecx,10h at the start of the TOC decrypt block (mov eax,[ebp-74h] before it has
// run). [ebp-54h] = archiveFlags; bit 4 = encrypted TOC. Plain: do what the block's tail does without
// its pushes (mov edx,[ebp-74h] / mov edi,[edx]; the add esp,28h balances only the skipped pushes).
RS7_STUB(Rs7_Stub_PP_TOC) {
    __asm {
        test byte ptr [ebp-54h], 4
        jz toc_plain
        mov ecx, 10h
        jmp g_rs7_resume_PP_TOC
    toc_plain:
        mov edx, dword ptr [ebp-74h]
        mov edi, dword ptr [edx]
        jmp g_rs7_skip_PP_TOC
    }
}

// PP_SNGF - replaces cmp dword ptr [esp+68h],3 (SNG flags) and resumes at the stock jne <reject>:
// flags 1 sets ZF like an equal compare (cmp eax,eax: the same flags as cmp 3,3), so the jne falls
// through to the signature check; anything else repeats the stock compare.
RS7_STUB(Rs7_Stub_PP_SNGF) {
    __asm {
        cmp dword ptr [esp+68h], 1
        jne sngf_stock
        cmp eax, eax
        jmp g_rs7_resume_PP_SNGF
    sngf_stock:
        cmp dword ptr [esp+68h], 3
        jmp g_rs7_resume_PP_SNGF
    }
}

// PP_SNGD - called in place of the game's SNG decrypt (cdecl, 6 args, bool in AL). src points just
// past the 8-byte SNG header and starts with the 16-byte IV slot; the game expects the payload at out[0].
using Rs7SngDecrypt = bool(__cdecl*)(const uint8_t* src, uint32_t len, const char* key, uint32_t keyLen, uint8_t* out,
                                     uint32_t outLen);

extern "C" __declspec(dllexport) bool __cdecl Rs7_Stub_PP_SNGD(const uint8_t* src, uint32_t len, const char* key,
                                                               uint32_t keyLen, uint8_t* out, uint32_t outLen) {
    // Even encrypted input needs the IV slot. Reject it before inspecting the preceding header.
    if (!src || len < 0x10) {
        return false;
    }

    uint32_t magic, flags;

    std::memcpy(&magic, src - 8, 4);
    std::memcpy(&flags, src - 4, 4);

    switch (Rs7Plain_SngAction(magic, flags, len, outLen)) {
        case Rs7SngAction::Copy:
            std::memmove(out, src + 0x10, len - 0x10);
            return true;
        case Rs7SngAction::Reject:
            return false;
        default:
            return reinterpret_cast<Rs7SngDecrypt>(g_rs7_target_PP_SNGD)(src, len, key, keyLen, out, outLen);
    }
}
