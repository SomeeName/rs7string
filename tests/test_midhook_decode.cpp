// Relocator whitelist: the position-independent encodings added for the stub conversion (INC/DEC r/m8, IMUL,
// TEST r/m,imm, operand-size prefix 66) and what stays rejected around them.
#include "platform/midhook.h"
#include "test.h"
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {
// Relocates `insn` followed by `jmp rel32` from 0x1000 to 0x2000. The jump is rewritten only when the decoder put the
// instruction boundary exactly at insn.size(), so a correct result proves the decoded length, not only acceptance.
bool RelocatesWithLength(std::initializer_list<uint8_t> insn) {
    std::vector<uint8_t> source(insn);
    const size_t n = source.size();

    source.insert(source.end(), {0xe9, 0, 0, 0, 0});
    uint8_t out[64]{};
    int32_t rel = 0;

    if (Rs7Mid_Relocate(out, sizeof out, source.data(), 0x1000, source.size(), 0x2000) != n + 5) {
        return false;
    }

    std::memcpy(&rel, out + n + 1, 4);

    return std::memcmp(out, source.data(), n) == 0 && out[n] == 0xe9 && rel == -0x1000;
}

// The instruction NOP-padded to 5 bytes (and to its own length) is rejected.
bool Rejected(std::initializer_list<uint8_t> insn) {
    std::vector<uint8_t> source(insn);

    source.resize(source.size() < 5 ? 5 : source.size(), 0x90);
    uint8_t out[64]{};

    return Rs7Mid_Relocate(out, sizeof out, source.data(), 0x1000, source.size(), 0x2000) == 0;
}
}  // namespace

TEST(midhook_decode_inc_dec_r8) {
    CHECK(RelocatesWithLength({0xfe, 0xc3}));                          // inc bl
    CHECK(RelocatesWithLength({0xfe, 0xcb}));                          // dec bl (TF1, PUBEDGE)
    CHECK(RelocatesWithLength({0xfe, 0x40, 0x08}));                    // inc byte [eax+8]
    CHECK(RelocatesWithLength({0xfe, 0x84, 0x24, 1, 0, 0, 0}));        // inc byte [esp+1] (SIB, disp32)
    CHECK(RelocatesWithLength({0xfe, 0x05, 0x10, 0x20, 0x30, 0x40}));  // inc byte [abs32]
    CHECK(Rejected({0xfe, 0xd3}));                                     // FE /2 is undefined
    CHECK(Rejected({0xfe, 0xff}));                                     // FE /7 is undefined
    CHECK(Rejected({0x90, 0x90, 0x90, 0x90, 0xfe}));                   // ModRM cut by the span end
}

TEST(midhook_decode_imul) {
    CHECK(RelocatesWithLength({0x0f, 0xaf, 0xc1}));                       // imul eax,ecx
    CHECK(RelocatesWithLength({0x0f, 0xaf, 0x44, 0x24, 0x08}));           // imul eax,[esp+8]
    CHECK(RelocatesWithLength({0x6b, 0xc0, 0x07}));                       // imul eax,eax,7
    CHECK(RelocatesWithLength({0x6b, 0x4e, 0x10, 0x0c}));                 // imul ecx,[esi+10h],0Ch
    CHECK(RelocatesWithLength({0x69, 0xc0, 0x78, 0x56, 0x34, 0x12}));     // imul eax,eax,12345678h
    CHECK(RelocatesWithLength({0x69, 0x8e, 0, 1, 0, 0, 0x2c, 1, 0, 0}));  // imul ecx,[esi+100h],12Ch
    CHECK(Rejected({0x69, 0xc0, 0x78, 0x56, 0x34}));                      // cut imm32
    CHECK(Rejected({0x0f, 0xae, 0xc1, 0x90, 0x90}));                      // 0F AE (fence/fxsave group)
}

TEST(midhook_decode_test_rm_imm) {
    CHECK(RelocatesWithLength({0xf7, 0xc1, 0, 0, 1, 0}));        // test ecx,10000h
    CHECK(RelocatesWithLength({0xf7, 0x46, 0x08, 4, 0, 0, 0}));  // test dword [esi+8],4
    CHECK(RelocatesWithLength({0xf6, 0xc1, 0x01}));              // test cl,1
    CHECK(RelocatesWithLength({0xf6, 0x40, 0x10, 0x80}));        // test byte [eax+10h],80h
    CHECK(Rejected({0xf7, 0xd1}));                               // not ecx (/2)
    CHECK(Rejected({0xf7, 0xf1}));                               // div ecx (/6)
    CHECK(Rejected({0xf6, 0xc9, 0x01}));                         // F6 /1
}

TEST(midhook_decode_operand_size_prefix) {
    CHECK(RelocatesWithLength({0x66, 0x89, 0x08}));                    // mov [eax],cx
    CHECK(RelocatesWithLength({0x66, 0x3b, 0x4e, 0x10}));              // cmp cx,[esi+10h] (PSU1/PSU2/PSM1 form)
    CHECK(RelocatesWithLength({0x66, 0xb8, 0x34, 0x12}));              // mov ax,1234h: imm16
    CHECK(RelocatesWithLength({0x66, 0x81, 0xf9, 0x34, 0x12}));        // cmp cx,1234h: imm16
    CHECK(RelocatesWithLength({0x66, 0x83, 0xf9, 0x05}));              // cmp cx,5: imm8 stays imm8
    CHECK(RelocatesWithLength({0x66, 0xc7, 0x00, 0x34, 0x12}));        // mov word [eax],1234h
    CHECK(RelocatesWithLength({0x66, 0x69, 0xc0, 0x34, 0x12}));        // imul ax,ax,1234h
    CHECK(RelocatesWithLength({0x66, 0x0f, 0xaf, 0xc1}));              // imul ax,cx
    CHECK(RelocatesWithLength({0x66, 0x0f, 0xb6, 0xc1}));              // movzx ax,cl
    CHECK(RelocatesWithLength({0x66, 0xa9, 0x34, 0x12}));              // test ax,1234h
    CHECK(RelocatesWithLength({0x66, 0x3d, 0x34, 0x12}));              // cmp ax,1234h
    CHECK(RelocatesWithLength({0x66, 0xa1, 0x10, 0x20, 0x30, 0x40}));  // mov ax,[moffs32]: address stays 4 bytes
    CHECK(RelocatesWithLength({0x66, 0x90}));                          // xchg ax,ax
    CHECK(RelocatesWithLength({0x66, 0x50}));                          // push ax
    CHECK(RelocatesWithLength({0x66, 0xfe, 0xc3}));                    // ignored on a byte form
    CHECK(Rejected({0x66, 0xe8, 0, 0}));                               // call rel16
    CHECK(Rejected({0x66, 0xe9, 0, 0}));                               // jmp rel16
    CHECK(Rejected({0x66, 0xeb, 0}));                                  // jmp short with 16-bit IP
    CHECK(Rejected({0x66, 0x75, 0}));
    CHECK(Rejected({0x66, 0x0f, 0x85, 0, 0}));
    CHECK(Rejected({0x66, 0xc3}));
    CHECK(Rejected({0x66, 0xd9, 0x00}));              // x87
    CHECK(Rejected({0x66, 0x0f, 0x94, 0xc0}));        // setcc
    CHECK(Rejected({0x66, 0x66, 0x90}));              // one prefix only
    CHECK(Rejected({0x90, 0x90, 0x90, 0x90, 0x66}));  // prefix cut by the span end
    CHECK(Rejected({0x67, 0x8b, 0x00}));              // address-size prefix
    CHECK(Rejected({0xf0, 0xfe, 0x00}));              // LOCK
    CHECK(Rejected({0x64, 0xa1, 0, 0, 0, 0}));        // FS segment
}
