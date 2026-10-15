"""Execute the production mid-hook builder and thunk in Unicorn, including real game sites."""

import struct
import pytest
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EFLAGS, UC_X86_REG_ESP
from test_emu import Emu, ALL_REGS, CODE, DATA, STACK, RET_MAGIC, DLL, SNAPSHOT, site_va, game_text, TEXT_VA

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)


def build(emu, raw, source=CODE + 0x1000, callback=CODE + 0x3000):
    out = CODE + 0x2000
    emu.read(source, len(raw))
    emu.uc.mem_write(source, raw)
    emu.uc.mem_write(callback, b"\xc3")
    esp = STACK + 0x70000
    args = [RET_MAGIC, out, 256, source, source, len(raw), out, callback]
    emu.uc.mem_write(esp, struct.pack("<8I", *args))
    emu.run(emu.exports["Rs7Mid_Build"], RET_MAGIC, {UC_X86_REG_ESP: esp})
    assert emu.uc.reg_read(UC_X86_REG_EAX) > 0
    return out, callback


@pytest.mark.parametrize("sid", ["D2", "D3"])
@pytest.mark.parametrize("flags", [0x202, 0x2D7, 0xED7])
def test_noop_real_site_all_registers_flags_stack(sid, flags):
    emu = Emu(False)
    va = site_va(sid)
    n = 5 if sid == "D2" else 6
    raw = game_text()[va - TEXT_VA : va - TEXT_VA + n]
    # Source bytes are copied to scratch by builder; relocation still uses their real VA.
    out, callback = build(emu, raw, source=va)
    regs = dict(
        zip(ALL_REGS, [0x111, 0x222, 0x333, 0x444, DATA + 0x100, DATA + 0x200, STACK + 0x81000, STACK + 0x80000])
    )
    regs[UC_X86_REG_EFLAGS] = flags
    emu.uc.mem_write(DATA + 0x214, struct.pack("<I", 3))
    results = []
    for start in (va, out):
        emu.uc.mem_write(STACK + 0x7FFC0, bytes(0x100))
        emu.run(start, va + n, regs)
        top = emu.uc.reg_read(UC_X86_REG_ESP)
        results.append((emu.regs(*ALL_REGS), emu.uc.reg_read(UC_X86_REG_EFLAGS), emu.read(top, 64)))
    assert results[0] == results[1]


@pytest.mark.parametrize("taken", [False, True])
def test_relocated_jcc_and_call(taken):
    emu = Emu(False)
    source = CODE + 0x1000
    callee = CODE + 0x4000
    # JZ skips a call, both paths join at the original continuation.
    raw = b"\x74\x05\xe8" + struct.pack("<i", callee - (source + 7))
    out, _ = build(emu, raw)
    emu.uc.mem_write(callee, b"\x40\xc3")
    regs = dict(zip(ALL_REGS, [10, 20, 30, 40, 50, 60, 70, STACK + 0x80000]))
    regs[UC_X86_REG_EFLAGS] = 0x242 if taken else 0x202
    states = []
    for start in (source, out):
        emu.run(start, source + 7, regs)
        states.append((emu.regs(*ALL_REGS), emu.uc.reg_read(UC_X86_REG_EFLAGS)))
    assert states[0] == states[1]
    assert states[1][0][0] == (10 if taken else 11)


@pytest.mark.parametrize("stack_delta", [-512, -48, 0, 8192])
def test_callback_mutates_registers_flags_stack_and_requests_jump(stack_delta):
    emu = Emu(False)
    out, callback = build(emu, b"\xb8\x07\x00\x00\x00")
    # Context pointer at [esp+4]; write every editable component, skip stolen MOV.
    target = CODE + 0x5000
    newesp = STACK + 0x80000 + stack_delta
    cb = bytearray(b"\x8b\x44\x24\x04")
    values = [11, 12, 13, newesp, 14, 15, 16, 42, 0x2D7]
    for off, value in enumerate(values):
        cb += b"\xc7\x40" + bytes([off * 4]) + struct.pack("<I", value)
    cb += b"\xc7\x40\x28" + struct.pack("<I", target) + b"\xc3"
    emu.uc.mem_write(callback, bytes(cb))
    emu.run(out, target, {UC_X86_REG_ESP: STACK + 0x80000})
    assert emu.regs(*ALL_REGS) == (42, 14, 16, 15, 12, 11, 13, newesp)
    assert emu.uc.reg_read(UC_X86_REG_EFLAGS) == 0x2D7


def test_callback_clobbers_x87_sse_but_thunk_restores_them():
    from unicorn.x86_const import UC_X86_REG_XMM0, UC_X86_REG_XMM7

    emu = Emu(False)
    out, callback = build(emu, b"\x90" * 5)
    # Initialize x87 with a nonempty stack; use FXSAVE for a complete comparison.
    init, save = CODE + 0x6000, CODE + 0x6100
    state = DATA + 0x8000
    emu.uc.mem_write(init, b"\xdb\xe3\xd9\xe8\xd9\xeb")  # fninit; fld1; fldpi
    emu.uc.mem_write(save, b"\x0f\xae\x05" + struct.pack("<I", state))
    emu.run(init, init + 6, {})
    emu.uc.reg_write(UC_X86_REG_XMM0, 0x123456789ABCDEF)
    emu.uc.reg_write(UC_X86_REG_XMM7, 0xFEDCBA987654321)
    emu.run(save, save + 7, {})
    before = emu.read(state, 512)
    emu.uc.mem_write(callback, bytes.fromhex("DB E3 0F 57 C0 0F 57 FF FD C3"))
    # callback dirties FPU, XMM and DF; generic restore must undo them all.
    emu.run(out, CODE + 0x1005, {UC_X86_REG_ESP: STACK + 0x80000, UC_X86_REG_EFLAGS: 0x202})
    assert emu.uc.reg_read(UC_X86_REG_EFLAGS) == 0x202
    emu.run(save, save + 7, {})
    after = emu.read(state, 512)
    # CONFIRMED Unicorn limitation: even bare FXSAVE/FNINIT/FXRSTOR loses
    # FIP/FDP (offsets 8..23). Native unit test checks the hardware state too.
    assert after[:8] == before[:8]
    assert after[24:] == before[24:]


@pytest.mark.parametrize("sid", ["XB1", "B1"])
def test_relocated_call_from_real_game_bytes(sid):
    emu = Emu(False)
    va = site_va(sid)
    raw = game_text()[va - TEXT_VA : va - TEXT_VA + 9]
    out, _ = build(emu, raw, source=va)
    # Execute the stock call target's return convention with deterministic code;
    # the original target itself needs the live game allocator/string runtime.
    callee = va + 9 + struct.unpack("<i", raw[5:9])[0]
    emu.read(callee, 1)
    saved = emu.read(callee, 16)
    emu.uc.mem_write(callee, b"\xb8\x78\x56\x34\x12" + (b"\xc2\x04\x00" if sid == "B1" else b"\xc3"))
    esp, ebp = STACK + 0x80000, STACK + 0x81000
    emu.uc.mem_write(ebp + 0x18, struct.pack("<I", DATA + 0x100))
    regs = dict(zip(ALL_REGS, [10, 20, 30, 40, 50, 60, ebp, esp]))
    regs[UC_X86_REG_EFLAGS] = 0x2D7
    results = []
    try:
        for start in (va, out):
            emu.uc.mem_write(esp, bytes(32))
            emu.run(start, va + 9, regs)
            top = emu.uc.reg_read(UC_X86_REG_ESP)
            results.append((emu.regs(*ALL_REGS), emu.uc.reg_read(UC_X86_REG_EFLAGS), emu.read(top, 32)))
        assert results[0] == results[1]
    finally:
        emu.uc.mem_write(callee, saved)


@pytest.mark.parametrize("taken", [False, True])
def test_relocated_jcc_from_real_game_bytes(taken):
    emu = Emu(False)
    va = site_va("S2")
    raw = game_text()[va - TEXT_VA : va - TEXT_VA + 5]  # cmp edx,5; ja rel8
    assert raw[:4] == bytes.fromhex("83 FA 05 77")
    out, _ = build(emu, raw, source=va)
    target = va + 5 + struct.unpack("<b", raw[4:5])[0]
    until = target if taken else va + 5
    from unicorn.x86_const import UC_X86_REG_EDX

    regs = dict(zip(ALL_REGS, [10, 20, 30, 40, 50, 60, 70, STACK + 0x80000]))
    regs[UC_X86_REG_EDX] = 6 if taken else 3
    regs[UC_X86_REG_EFLAGS] = 0x2D7
    states = []
    for start in (va, out):
        emu.run(start, until, regs)
        states.append((emu.regs(*ALL_REGS), emu.uc.reg_read(UC_X86_REG_EFLAGS)))
    assert states[0] == states[1]


@pytest.mark.parametrize(
    "raw",
    [
        "EB 03 90 90 90",  # rel8 JMP to span end
        "E9 00 00 00 00",  # rel32 JMP to span end
        "0F 84 01 00 00 00 40",  # rel32 JZ skips INC
        "EB 01 40 41 90",  # rel8 JMP to internal instruction
        "E9 01 00 00 00 40 41",  # rel32 JMP to internal instruction
        "40 75 FD 90 90",  # backward JNZ, bounded by wraparound
    ],
)
@pytest.mark.parametrize("flags", [0x202, 0x242])
def test_relative_branch_forms_and_internal_targets(raw, flags):
    emu = Emu(False)
    raw = bytes.fromhex(raw)
    out, _ = build(emu, raw)
    regs = dict(zip(ALL_REGS, [0xFFFFFFFE, 20, 30, 40, 50, 60, 70, STACK + 0x80000]))
    regs[UC_X86_REG_EFLAGS] = flags
    results = []
    for start in (CODE + 0x1000, out):
        emu.run(start, CODE + 0x1000 + len(raw), regs)
        results.append((emu.regs(*ALL_REGS), emu.uc.reg_read(UC_X86_REG_EFLAGS)))
    assert results[0] == results[1]


# [neck] clite (core/clite.h): the GK* mid-hook spans replay exactly like the original bytes.
@pytest.mark.parametrize(
    "sid,n", [("GKC", 11), ("GKE", 6), ("GKM", 5), ("GKL", 5), ("GKT", 5), ("GKH1", 5), ("GKH2", 6)]
)
def test_clite_sites_replay_like_the_game(sid, n):
    emu = Emu(False)
    va = site_va(sid)
    raw = game_text()[va - TEXT_VA : va - TEXT_VA + n]
    out, _ = build(emu, raw, source=va)
    # EBX -> a "manager" whose +0Ch points at a readable dword; ESI / EDI profiles; EBP a frame.
    emu.uc.mem_write(DATA + 0x400, bytes(0x400))
    emu.uc.mem_write(DATA + 0x40C, struct.pack("<I", DATA + 0x500))
    emu.uc.mem_write(DATA + 0x500, struct.pack("<I", 0x12345678))
    emu.uc.mem_write(DATA + 0x608, struct.pack("<I", DATA + 0x638))  # GKL: [esi+8] -> a writable result slot
    emu.uc.mem_write(DATA + 0x613, b"\x01")
    emu.uc.mem_write(DATA + 0x713, b"\x02")
    # GKH1 / GKH2: EBX / [EBP-40h] -> an InstrumentInfo whose +0Bh count is 7.
    emu.uc.mem_write(DATA + 0x40B, bytes([7]))
    emu.uc.mem_write(DATA + 0x48B, bytes([7]))
    emu.uc.mem_write(STACK + 0x81000 - 0x40, struct.pack("<I", DATA + 0x480))
    regs = dict(
        zip(
            ALL_REGS,
            [0x111, DATA + 0x400, 0x333, DATA + 0x480, DATA + 0x600, DATA + 0x700, STACK + 0x81000, STACK + 0x80000],
        )
    )
    regs[UC_X86_REG_EFLAGS] = 0x202
    results = []
    for start in (va, out):
        emu.uc.mem_write(DATA + 0x638, bytes(8))
        emu.uc.mem_write(STACK + 0x81008, struct.pack("<I", DATA + 0x900))
        emu.run(start, va + n, regs)
        results.append((emu.regs(*ALL_REGS), emu.uc.reg_read(UC_X86_REG_EFLAGS) & 0x8D5, emu.read(DATA + 0x638, 8)))
    assert results[0] == results[1]
