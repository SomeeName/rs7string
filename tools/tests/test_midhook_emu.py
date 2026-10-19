"""Execute the production mid-hook builder and thunk in Unicorn, including real game sites."""

import struct
import pytest
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EFLAGS, UC_X86_REG_ESP
from emu_harness import (
    ALL_REGS,
    CODE,
    CS,
    DATA,
    DLL,
    LIGHT_CALLBACKS,
    MID_CALLBACKS,
    SSE_CALLBACKS,
    RET_MAGIC,
    SNAPSHOT,
    STACK,
    TEXT_VA,
    Emu,
    game_text,
    site_va,
)

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)


BUILDERS = ["Rs7Mid_Build", "Rs7Mid_BuildLight", "Rs7Mid_BuildSse"]


def build(emu, raw, source=CODE + 0x1000, callback=CODE + 0x3000, builder="Rs7Mid_Build"):
    out = CODE + 0x2000
    emu.read(source, len(raw))
    emu.uc.mem_write(source, raw)
    emu.uc.mem_write(callback, b"\xc3")
    esp = STACK + 0x70000
    args = [RET_MAGIC, out, 256, source, source, len(raw), out, callback]
    emu.uc.mem_write(esp, struct.pack("<8I", *args))
    emu.run(emu.exports[builder], RET_MAGIC, {UC_X86_REG_ESP: esp})
    assert emu.uc.reg_read(UC_X86_REG_EAX) > 0
    return out, callback


@pytest.mark.parametrize("sid", ["D2", "D3"])
@pytest.mark.parametrize("flags", [0x202, 0x2D7, 0xED7])
@pytest.mark.parametrize("builder", BUILDERS)
def test_noop_real_site_all_registers_flags_stack(sid, flags, builder):
    emu = Emu(False)
    va = site_va(sid)
    n = 5 if sid == "D2" else 6
    raw = game_text()[va - TEXT_VA : va - TEXT_VA + n]
    # Source bytes are copied to scratch by builder; relocation still uses their real VA.
    out, callback = build(emu, raw, source=va, builder=builder)
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
@pytest.mark.parametrize("builder", BUILDERS)
def test_relocated_jcc_and_call(taken, builder):
    emu = Emu(False)
    source = CODE + 0x1000
    callee = CODE + 0x4000
    # JZ skips a call, both paths join at the original continuation.
    raw = b"\x74\x05\xe8" + struct.pack("<i", callee - (source + 7))
    out, _ = build(emu, raw, builder=builder)
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
@pytest.mark.parametrize("builder", BUILDERS)
def test_callback_mutates_registers_flags_stack_and_requests_jump(stack_delta, builder):
    emu = Emu(False)
    out, callback = build(emu, b"\xb8\x07\x00\x00\x00", builder=builder)
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


# [neck] clite (features/clite/clite.h): the GK* mid-hook spans replay exactly like the original bytes.
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


# PGH1 (features/headstock/assetload_pegs.cpp): `push 0Eh; fstp [ebp-10h]` replays like the game when the mesh is drawn.
def test_pgh1_site_replays_like_the_game():
    emu = Emu(False)
    va = site_va("PGH1")
    raw = game_text()[va - TEXT_VA : va - TEXT_VA + 5]
    assert raw == bytes.fromhex("6A 0E D9 5D F0")
    out, _ = build(emu, raw, source=va)
    regs = dict(zip(ALL_REGS, [0x111, 0x222, 0x333, DATA + 0x480, 0x555, 0x666, STACK + 0x81000, STACK + 0x80000]))
    regs[UC_X86_REG_EFLAGS] = 0x202
    results = []
    for start in (va, out):
        emu.uc.mem_write(STACK + 0x80000 - 4, bytes(4))
        emu.run(start, va + 5, regs)
        results.append((emu.regs(*ALL_REGS), emu.read(STACK + 0x80000 - 4, 4)))
    assert results[0] == results[1]
    assert results[0][1] == struct.pack("<I", 0x0E)


# Light mid-hook (Rs7Mid::Mode::Light): the thunk executes no x87/MMX/SSE instruction at all. CR0.TS makes every such
# instruction raise #NM, so a run that completes proves it; the FXSAVE image around the run is compared as well.
def _fp_state(emu, value):
    from unicorn.x86_const import UC_X86_REG_XMM0, UC_X86_REG_XMM7

    init, save, state = CODE + 0x6000, CODE + 0x6100, DATA + 0x8000
    emu.uc.mem_write(init, bytes.fromhex("DB E3 D9 E8 D9 EB"))  # fninit; fld1; fldpi
    emu.uc.mem_write(save, bytes.fromhex("0F AE 05") + struct.pack("<I", state))  # fxsave [state]
    if value:
        emu.run(init, init + 6, {})
        emu.uc.reg_write(UC_X86_REG_XMM0, 0x123456789ABCDEF)
        emu.uc.reg_write(UC_X86_REG_XMM7, 0xFEDCBA987654321)
    emu.run(save, save + 7, {})
    return emu.read(state, 512)


def _set_ts(emu, on):
    from unicorn.x86_const import UC_X86_REG_CR0

    cr0 = emu.uc.reg_read(UC_X86_REG_CR0)
    emu.uc.reg_write(UC_X86_REG_CR0, cr0 | 8 if on else cr0 & ~8)


@pytest.mark.parametrize("flags", [0x202, 0x2D7, 0xED7])
def test_light_thunk_executes_no_fp_and_keeps_fp_state(flags):
    from unicorn import UcError

    emu = Emu(False)
    out, callback = build(emu, bytes.fromhex("90 90 90 90 90"), builder="Rs7Mid_BuildLight")
    before = _fp_state(emu, True)
    regs = dict(zip(ALL_REGS, [0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777, STACK + 0x80000]))
    regs[UC_X86_REG_EFLAGS] = flags
    _set_ts(emu, True)
    try:
        emu.run(out, CODE + 0x1005, regs)
    finally:
        _set_ts(emu, False)
    assert emu.regs(*ALL_REGS) == tuple(regs[r] for r in ALL_REGS)
    assert emu.uc.reg_read(UC_X86_REG_EFLAGS) == flags
    assert _fp_state(emu, False) == before
    # The check has teeth: the full thunk (FXSAVE) faults under CR0.TS.
    full, _ = build(emu, bytes.fromhex("90 90 90 90 90"), builder="Rs7Mid_Build")
    _set_ts(emu, True)
    try:
        with pytest.raises(UcError):
            emu.uc.emu_start(full, CODE + 0x1005, count=5000)
    finally:
        _set_ts(emu, False)


def _fp_instructions(emu, start, sse=False):
    """x87/MMX/SSE instructions reachable from `start` by direct branches and calls (recursive descent; indirect
    calls are not followed, an indirect jump fails the scan). sse: SSE on XMM registers (and LDMXCSR / STMXCSR) is
    allowed, x87, MMX and AVX (YMM) are still found."""
    import re

    fp_operand = re.compile(r"\by?mm\d|\bst\(" if sse else r"\b[xy]?mm\d|\bst\(")
    seen, todo, found = set(), [start], []
    while todo:
        address = todo.pop()
        while address not in seen:
            seen.add(address)
            insn = next(CS.disasm(emu.read(address, 16), address, 1))
            m, ops = insn.mnemonic, insn.op_str
            mxcsr = m in ("ldmxcsr", "stmxcsr") and not sse
            if m.startswith("f") or m in ("emms", "wait") or mxcsr or fp_operand.search(ops):
                found.append(f"{insn.address:#x} {m} {ops}")
            address = insn.address + insn.size
            direct = ops.startswith("0x")
            if m == "call" and direct:
                todo.append(int(ops, 16))
            elif m.startswith("j") and direct:
                todo.append(int(ops, 16))
                if m == "jmp":
                    break
            elif m == "jmp":
                found.append(f"{insn.address:#x} indirect jmp {ops} (not verifiable)")
                break
            elif m.startswith("ret") or m == "int3":
                break
    return found


def test_fp_scan_flags_the_full_thunk_and_passes_the_light_one():
    emu = Emu(False)
    assert any("fxsave" in line for line in _fp_instructions(emu, emu.exports["Rs7Mid_Thunk"]))
    assert _fp_instructions(emu, emu.exports["Rs7Mid_ThunkLight"]) == []


@pytest.mark.parametrize("sid", sorted(LIGHT_CALLBACKS))
def test_light_callbacks_use_no_fp(sid):
    emu = Emu(False)
    assert _fp_instructions(emu, emu.exports[MID_CALLBACKS[sid]]) == []


@pytest.mark.parametrize("sid", sorted(SSE_CALLBACKS))
def test_sse_callbacks_use_no_x87_or_mmx(sid):
    emu = Emu(False)
    assert _fp_instructions(emu, emu.exports[MID_CALLBACKS[sid]], sse=True) == []


def test_fp_scan_in_sse_mode_allows_sse_and_still_finds_x87():
    """The sse scan has teeth: the SSE thunk's only x87 instruction is its result load, the full thunk's FXSAVE /
    FNINIT are found, and the SSE thunk's MOVUPS / LDMXCSR are allowed."""
    emu = Emu(False)
    sse = _fp_instructions(emu, emu.exports["Rs7Mid_ThunkSse"], sse=True)
    assert len(sse) == 1 and "fld dword ptr [ebp + 0x2c]" in sse[0], sse
    assert any("movups" in line for line in _fp_instructions(emu, emu.exports["Rs7Mid_ThunkSse"]))
    assert any("fxsave" in line for line in _fp_instructions(emu, emu.exports["Rs7Mid_Thunk"], sse=True))


# Sse mid-hook (Rs7Mid::Mode::Sse): XMM0-7 and MXCSR restored, the x87 state untouched (no FXSAVE / FNINIT), and the
# callback's float loaded with `fld dword` when it requests one (Rs7Mid::LoadSt0). The callbacks are bytes: they
# clobber every XMM register and MXCSR and optionally store a result in the block Context::fpu points at.
def _sse_callback(load: int | None) -> bytes:
    code = bytearray()
    for r in range(8):  # pcmpeqd xmmR, xmmR (all ones)
        code += bytes([0x66, 0x0F, 0x76, 0xC0 | r << 3 | r])
    code += bytes.fromhex("6A 00 0F AE 14 24 58")  # push 0; ldmxcsr [esp] (all exceptions unmasked); pop eax
    if load is not None:
        code += bytes.fromhex("8B 44 24 04 8B 48 24")  # mov eax,[esp+4]; mov ecx,[eax+24h] (Context::fpu)
        code += bytes.fromhex("F6 C1 01 74 10")  # test cl,1; jz done (not sse mode: no block)
        code += bytes.fromhex("83 E1 FE C7 01") + struct.pack("<I", load)  # and ecx,-2; mov [ecx],value
        code += bytes.fromhex("C7 41 04 01 00 00 00")  # mov dword [ecx+4],1
    return bytes(code) + b"\xc3"


@pytest.mark.parametrize("flags", [0x202, 0x2D7, 0xED7])
@pytest.mark.parametrize("load", [None, 0x41200000])  # 10.0f
def test_sse_thunk_restores_sse_keeps_x87_and_loads_the_result(flags, load):
    from unicorn.x86_const import UC_X86_REG_FPSW, UC_X86_REG_MXCSR

    emu = Emu(False)
    out, callback = build(emu, bytes.fromhex("90 90 90 90 90"), builder="Rs7Mid_BuildSse")
    emu.uc.mem_write(callback, _sse_callback(load))
    before = _fp_state(emu, True)  # fld1; fldpi; XMM0 / XMM7 markers
    mxcsr = emu.uc.reg_read(UC_X86_REG_MXCSR)
    regs = dict(zip(ALL_REGS, [0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777, STACK + 0x80000]))
    regs[UC_X86_REG_EFLAGS] = flags
    emu.run(out, CODE + 0x1005, regs)
    assert emu.regs(*ALL_REGS) == tuple(regs[r] for r in ALL_REGS)
    assert emu.uc.reg_read(UC_X86_REG_EFLAGS) == flags
    assert emu.uc.reg_read(UC_X86_REG_MXCSR) == mxcsr
    if load is not None:
        assert (emu.uc.reg_read(UC_X86_REG_FPSW) >> 11) & 7 == 5  # three values: TOP 8 - 3
        assert emu.st0() == 10.0  # pops it again
    after = _fp_state(emu, False)
    # FIP/FDP: see the full-mode test above; the empty ST7 slot keeps the popped value's bits
    assert after[:8] == before[:8] and after[24:144] == before[24:144] and after[160:] == before[160:]


def test_sse_thunk_callback_sees_default_mxcsr_and_the_game_x87_stack():
    """The callback runs with MXCSR 1F80h (not the game's) on the game's x87 stack (no FNINIT): it records both."""
    from unicorn.x86_const import UC_X86_REG_MXCSR

    emu = Emu(False)
    out, callback = build(emu, bytes.fromhex("90 90 90 90 90"), builder="Rs7Mid_BuildSse")
    seen = DATA + 0x9000
    record = bytes.fromhex("0F AE 1D") + struct.pack("<I", seen)  # stmxcsr [seen]
    record += bytes.fromhex("DF E0 66 A3") + struct.pack("<I", seen + 4)  # fnstsw ax; mov [seen+4],ax (test only)
    emu.uc.mem_write(callback, record + b"\xc3")
    _fp_state(emu, True)
    emu.uc.reg_write(UC_X86_REG_MXCSR, 0x9FC0)  # FTZ + DAZ: the game's value must not reach the callback
    emu.run(out, CODE + 0x1005, {UC_X86_REG_ESP: STACK + 0x80000})
    assert struct.unpack("<I", emu.read(seen, 4))[0] == 0x1F80
    assert (struct.unpack("<H", emu.read(seen + 4, 2))[0] >> 11) & 7 == 6  # fld1; fldpi still on the stack
    assert emu.uc.reg_read(UC_X86_REG_MXCSR) == 0x9FC0
