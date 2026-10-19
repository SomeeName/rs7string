"""Native N-slot PegSet stubs (PSC1, PSD, PSU, PSM, PSA2, PSL, PSR1, PSN) against the stock PegSet."""

from __future__ import annotations

import struct

import pytest
from unicorn import UC_HOOK_CODE, UC_PROT_ALL
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EIP,
    UC_X86_REG_ESI,
    UC_X86_REG_ESP,
)

import sitetable
from emu_harness import (
    ALL_REGS,
    CODE,
    DATA,
    Emu,
    RET_MAGIC,
    S7_EBP,
    S7_ESP,
    S7_MEM,
    S7_REGS,
    SITES,
    needs_emu,
    run_s7_site,
    site_va,
    snapshot,
    stack,
)

pytestmark = needs_emu


HEAD_MEM = DATA + 0x30000  # guitar / headstock / PegSet for the native PegSet tests
PEG_MAGIC = 0x50375352  # "RS7P" at PegSet +1D0h (features/headstock/pegset.h; diagnostic only, review #1)
PEG_OWNERS = 64  # RS7_PEG_OWNERS: the native PegSet ownership table g_rs7_pegsets
PEG_MODES = ["native", "stock", "stale-tail"]  # registered / boot PegSet / rs7 tail bytes but not registered


def set_owners(emu: Emu, *pegsets: int):
    """g_rs7_pegsets = pegsets (the rest free). Native identification is this table only (review #1)."""
    tbl = list(pegsets) + [0] * (PEG_OWNERS - len(pegsets))
    emu.uc.mem_write(emu.exports["g_rs7_pegsets"], struct.pack(f"<{PEG_OWNERS}I", *tbl))


def owners(emu: Emu) -> list[int]:
    return [x for x in struct.unpack(f"<{PEG_OWNERS}I", emu.read(emu.exports["g_rs7_pegsets"], PEG_OWNERS * 4)) if x]


@pytest.fixture
def pegs(emus):
    """An empty ownership table in both emulators, before and after the test (module-scoped emulators)."""
    for e in emus:
        set_owners(e)
    yield
    for e in emus:
        set_owners(e)


def own_for(emus, ps: int, mode: str):
    for e in emus:
        set_owners(e, *([ps] if mode == "native" else []))


def set_head(emu: Emu, on: int):
    emu.uc.mem_write(emu.exports["g_rs7_head_on"], struct.pack("<I", on))


@pytest.fixture(params=[1, 0], ids=["headstock_peg", "switch-off"])
def head_on(request, emus):
    _, patched = emus
    set_head(patched, request.param)
    yield request.param
    set_head(patched, 1)


# ---- the native N-slot PegSet (headstock.h, native_pegset=1) ----


def run_ctor(emu: Emu, ps: int) -> bytes:
    emu.uc.mem_write(ps, b"\xcd" * 0x1D4)
    emu.uc.mem_write(S7_ESP, stack(RET_MAGIC))
    cm = emu.read(0x135F58C, 4)  # other tests point the colour manager at a fake: the ctor needs the real one
    emu.uc.mem_write(0x135F58C, snapshot().read(0x135F58C, 4))
    try:
        emu.run(0x59C190, RET_MAGIC, {UC_X86_REG_ESP: S7_ESP, UC_X86_REG_EDI: ps, UC_X86_REG_EBP: S7_EBP})
    finally:
        emu.uc.mem_write(0x135F58C, cm)
    assert emu.uc.reg_read(UC_X86_REG_EAX) == ps and emu.uc.reg_read(UC_X86_REG_EDI) == ps
    assert emu.fpu_top() == 0
    return emu.read(ps, 0x1D4)


def test_psc1_ctor_is_stock_for_the_six_and_extends_to_eight(emus, pegs):
    """Rnd_PegSet_Ctor with PSC1: header and slots 0-5 exactly as stock, slots 6/7 = slot 0's fresh
    defaults, pegColor[0..5] moved from +0FCh to +14Ch ([6]/[7] = [0]), the rs7 tail and magic."""
    stock, patched = (run_ctor(e, HEAD_MEM) for e in emus)
    assert patched[:0xFC] == stock[:0xFC]

    def slot(m, s):
        return m[0x0C + s * 0x28 : 0x0C + (s + 1) * 0x28]

    for s in (6, 7):
        assert slot(patched, s) == slot(stock, 0)
    for i in range(8):
        src = 0xFC + (i if i < 6 else 0) * 0x10
        assert patched[0x14C + i * 0x10 : 0x15C + i * 0x10] == stock[src : src + 0x10]
    assert patched[0x1CC:0x1D0] == bytes([8, 0, 0, 0])
    assert struct.unpack_from("<I", patched, 0x1D0)[0] == PEG_MAGIC
    assert stock[0x15C:0x1D4] == b"\xcd" * 0x78  # stock writes nothing past its 0x15C bytes
    assert owners(emus[1]) == [HEAD_MEM] and owners(emus[0]) == []  # PSC1 registered it (review #1)


def test_psc1_full_ownership_table_leaves_the_pegset_stock(emus, pegs):
    """No free entry: Rs7_PegSetCtor leaves the stock ctor's image (6 slots, pegColor at +0FCh)."""
    full = [DATA + 0x80000 + i * 0x10 for i in range(PEG_OWNERS)]
    set_owners(emus[1], *full)
    stock, patched = (run_ctor(e, HEAD_MEM) for e in emus)
    assert patched == stock
    assert owners(emus[1]) == full


DEC_FN = CODE + 0x9000  # stand-in for the InterlockedDecrement import: lock dec [arg]; ret 4
FAKE_FN = CODE + 0x9100  # 0x20 bytes per fake object: +0 delete (vtbl+4), +10h release (vtbl+8), both `ret`
FAKE_OBJ = DATA + 0x90000


def fake_pegset(emu: Emu, ps: int, slots: int, native: bool = True):
    """slots x (entity +4, locator +8, owning +0Ch) fake objects with their own vtables; refcount 1.
    native: the rs7 tail (capacity 8 at +1CCh, magic at +1D0h) as Rs7_PegSetCtor writes it."""
    emu.uc.mem_write(DEC_FN, bytes.fromhex("8B 44 24 04 F0 FF 08 8B 00 C2 04 00"))
    emu.uc.mem_write(FAKE_FN, b"\xc3" * 0x20 * 0x20)
    img = bytearray(0x1D4)
    for s in range(slots):
        for k, off in enumerate((0x04, 0x08, 0x0C)):
            n = s * 3 + k
            obj, vt = FAKE_OBJ + n * 0x20, FAKE_OBJ + 0x1000 + n * 0x10
            emu.uc.mem_write(obj, stack(vt, 1))
            emu.uc.mem_write(vt, stack(0, FAKE_FN + n * 0x20, FAKE_FN + n * 0x20 + 0x10))
            struct.pack_into("<I", img, 0x0C + s * 0x28 + off, obj)
    if native:
        img[0x1CC] = 8
        struct.pack_into("<I", img, 0x1D0, PEG_MAGIC)
    emu.uc.mem_write(ps, bytes(img))


def run_dtor(emu: Emu, ps: int) -> list[int]:
    iat = struct.unpack("<I", emu.read(0x59C307, 4))[0]  # mov ebx,[InterlockedDecrement]
    old = emu.read(iat, 4)
    emu.uc.mem_write(iat, stack(DEC_FN))
    hits: list[int] = []
    h = emu.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: hits.append(a), begin=FAKE_FN, end=FAKE_FN + 0x400)
    try:
        emu.uc.mem_write(S7_ESP, stack(RET_MAGIC))
        emu.run(0x59C300, RET_MAGIC, {UC_X86_REG_ESP: S7_ESP, UC_X86_REG_EAX: ps, UC_X86_REG_EBP: S7_EBP})
    finally:
        emu.uc.hook_del(h)
        emu.uc.mem_write(iat, old)
    return [a - FAKE_FN for a in hits]


@pytest.mark.parametrize("mode", PEG_MODES)
def test_psd_dtor_releases_eight_slots_in_the_stock_order(emus, pegs, mode):
    """Slot s: the owning +0Ch pointer deleted, the locator deleted, the entity released, s = 7..0
    for a native (registered) PegSet; the stock dtor - and the patched one on a PegSet constructed
    before the patch, even with rs7 tail bytes - does the same for 5..0."""
    stock, patched = emus
    native = mode == "native"
    fake_pegset(stock, HEAD_MEM, 8, mode != "stock")
    fake_pegset(patched, HEAD_MEM, 8, mode != "stock")
    own_for(emus, HEAD_MEM, mode)

    def want(top):
        return [x for s in range(top, -1, -1) for x in ((s * 3 + 2) * 0x20, (s * 3 + 1) * 0x20, s * 3 * 0x20 + 0x10)]

    assert run_dtor(stock, HEAD_MEM) == want(5)
    assert run_dtor(patched, HEAD_MEM) == want(7 if native else 5)


def peg_img(count: int, tail: bool) -> bytes:
    """tail: the rs7 tail bytes (capacity 8, magic) - which do not make a PegSet native (review #1)."""
    img = bytearray(0x1D4)
    struct.pack_into("<H", img, 4, count)
    if tail:
        img[0x1CC] = 8
        struct.pack_into("<I", img, 0x1D0, PEG_MAGIC)
    return bytes(img)


PEG_EXITS = {
    "PSU1": (0x59C3C8, 0x59C41A),
    "PSU2": (0x59C41A, 0x59C3D0),  # (fall-through, branch)
    "PSM1": (0x541BA3, 0x541D2B),
    "PSM2": (0x541D3D, 0x541B90),
}


def run_peg_branch(emu: Emu, sid: str, regs: dict[int, int], mem: dict[int, bytes]):
    """From the site to whichever exit it takes: (exit, registers)."""
    fall, branch = PEG_EXITS[sid]
    for a, b in mem.items():
        emu.uc.mem_write(a, b)
    h = emu.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: uc.emu_stop(), begin=branch, end=branch)
    try:
        for r, v in {**S7_REGS, **regs}.items():
            emu.uc.reg_write(r, v)
        emu.uc.emu_start(site_va(sid), fall, count=2000)  # the ownership scan: up to 64 entries
    finally:
        emu.uc.hook_del(h)
    eip = emu.uc.reg_read(UC_X86_REG_EIP)
    assert eip in (fall, branch), f"{sid} stopped at {eip:#x}"
    return eip, emu.regs(*ALL_REGS)


def peg_regs(sid: str, ps: int, i: int) -> tuple[dict[int, int], dict[int, bytes]]:
    holder = HEAD_MEM + 0x400  # PSM2: [[ebp+8]+8] = PegSet
    return {
        "PSU1": ({UC_X86_REG_EDI: ps, UC_X86_REG_ECX: i}, {}),
        "PSU2": ({UC_X86_REG_EDI: ps, UC_X86_REG_EBX: i}, {}),
        "PSM1": ({UC_X86_REG_EAX: ps, UC_X86_REG_ECX: i}, {}),
        "PSM2": ({UC_X86_REG_EBX: i}, {S7_EBP + 8: stack(holder), holder: stack(0, 0, ps)}),
    }[sid]


@pytest.mark.parametrize("sid", ["PSU1", "PSU2", "PSM1", "PSM2"])
@pytest.mark.parametrize("i", [0, 5, 6, 7, 8])
@pytest.mark.parametrize("mode", PEG_MODES)
def test_peg_slot_bounds_eight_only_for_a_native_pegset(emus, pegs, sid, i, mode):
    """Unload give-back loop (PSU1 entry / PSU2 latch) and the string renderer's peg mesh pass
    (PSM1 slot bound / PSM2 pass latch): bound 8 for a native PegSet, the stock bound ([+4] = 6, or
    6 passes) for a PegSet constructed before the patch - stock exit, stock registers."""
    ps = HEAD_MEM
    native = mode == "native"
    own_for(emus, ps, mode)
    regs, extra = peg_regs(sid, ps, i)
    out = [run_peg_branch(emu, sid, regs, {ps: peg_img(6, mode != "stock"), **extra}) for emu in emus]
    fall, branch = PEG_EXITS[sid]
    stock_in = i < 6
    patched_in = i < (8 if native else 6)
    inside = {"PSU1": fall, "PSU2": branch, "PSM1": fall, "PSM2": branch}[sid]  # the exit "still a slot"
    outside = branch if inside == fall else fall
    assert out[0][0] == (inside if stock_in else outside)
    assert out[1][0] == (inside if patched_in else outside)
    assert out[1][1] == out[0][1]


@pytest.mark.parametrize("mode", PEG_MODES)
def test_psu3_taken_flag_clear_count(emus, pegs, mode):
    ps = HEAD_MEM
    native = mode == "native"
    own_for(emus, ps, mode)
    stock, patched = run_s7_site(
        emus, "PSU3", {UC_X86_REG_EDI: ps, UC_X86_REG_EAX: ps + 0x0C}, {ps: peg_img(6, mode != "stock")}
    )
    assert stock[0][2] == 6 and patched[0][2] == (8 if native else 6)
    assert patched[0][:2] == stock[0][:2] and patched[0][3:] == stock[0][3:]


@pytest.mark.parametrize("mode", PEG_MODES)
def test_psa2_sized_free_matches_the_allocation(emus, pegs, mode):
    """The headstock destructor frees a native PegSet with 1D4h (PSA1's allocation), one constructed
    before the patch with the stock 15Ch. A native PegSet leaves the ownership table there (before
    the free: a PegSet later constructed at that address registers afresh); other entries stay."""
    ps, other = HEAD_MEM, HEAD_MEM + 0x800
    native = mode == "native"
    for e in emus:
        set_owners(e, *([other, ps] if native else [other]))
    stock, patched = run_s7_site(emus, "PSA2", {UC_X86_REG_ESI: ps}, {ps: peg_img(6, mode != "stock")})
    assert stock[3][:4] == stack(0x15C) and patched[3][:4] == stack(0x1D4 if native else 0x15C)
    assert patched[0] == stock[0]
    assert owners(emus[1]) == [other]


EDGE_PAGE = 0x5D380000  # one page with nothing mapped (or mappable from the snapshot) after it


@pytest.fixture(scope="module")
def edge_page(emus):
    """Maps EDGE_PAGE once per module and never unmaps it (Unicorn 2 mem_unmap between runs of cached code
    gave spurious fetch faults)."""
    import bisect

    end = EDGE_PAGE + 0x1000
    for e in emus:
        assert all(not (b <= end <= t) and not (b <= EDGE_PAGE <= t) for b, t, _ in e.uc.mem_regions())
        i = bisect.bisect_right(e.mem.bases, end) - 1
        assert i < 0 or e.mem.regions[i]["base"] + e.mem.regions[i]["size"] <= EDGE_PAGE
        e.uc.mem_map(EDGE_PAGE, 0x1000, UC_PROT_ALL)
    return end


@pytest.fixture
def edge_pegset(edge_page, pegs):
    """A stock (boot) PegSet whose 15Ch bytes end exactly at the end of a mapped page with nothing
    readable after it: any read of the old +1CCh/+1D0h tail check faults (review #1)."""
    return edge_page - 0x15C


@pytest.mark.parametrize("sid", ["PSA2", "PSD1", "PSD2", "PSU3", "PSL1"])
def test_peg_stock_pegset_at_a_page_end_is_never_read_past_its_allocation(emus, edge_pegset, sid):
    """Review #1: the native decision reads only the ownership table, so a stock 15Ch PegSet at the
    end of the heap's last readable page runs every site exactly as stock (the old magic check read
    74h bytes past it and faulted here)."""
    ps = edge_pegset
    img = bytearray(0x15C)
    struct.pack_into("<H", img, 4, 6)
    info = HEAD_MEM + 0x400
    inf = bytearray(0x40)
    inf[0x0B] = 6
    reg = {
        "PSA2": UC_X86_REG_ESI,
        "PSD1": UC_X86_REG_EAX,
        "PSD2": UC_X86_REG_EAX,
        "PSU3": UC_X86_REG_EDI,
        "PSL1": UC_X86_REG_EBX,
    }[sid]
    regs = {reg: ps, UC_X86_REG_EDI: info} if sid == "PSL1" else {reg: ps}
    if sid == "PSU3":
        regs[UC_X86_REG_EAX] = ps + 0x0C
    for e in emus:  # the dtor tests ran PSD1/PSD2's continuations: cached blocks would miss run()'s `until`
        e.uc.ctl_flush_tb()
    stock, patched = run_s7_site(emus, sid, regs, {ps: bytes(img), info: bytes(inf)})
    assert patched[0] == stock[0] and patched[3] == stock[3] and patched[4] == stock[4]
    if sid == "PSA2":
        assert patched[3][:4] == stack(0x15C)


@pytest.mark.parametrize("sid", ["PSU1", "PSU2", "PSM1", "PSM2"])
def test_peg_bounds_stock_pegset_at_a_page_end(emus, edge_pegset, sid):
    ps = edge_pegset
    img = bytearray(0x15C)
    struct.pack_into("<H", img, 4, 6)
    regs, extra = peg_regs(sid, ps, 6)
    out = [run_peg_branch(emu, sid, regs, {ps: bytes(img), **extra}) for emu in emus]
    assert out[1] == out[0]


@pytest.mark.parametrize(
    "count,mode,want", [(6, "native", 7), (4, "native", 4), (6, "stock", 6), (6, "stale-tail", 6), (5, "native", 5)]
)
def test_psl1_load_count(emus, pegs, head_on, count, mode, want):
    """A 6-string guitar's native PegSet loads 7 (switch on); bass, a PegSet constructed before the
    patch (with or without rs7 tail bytes) and the switch off load the instrument's count. ECX = 0."""
    info, ps = HEAD_MEM + 0x400, HEAD_MEM
    own_for(emus, ps, mode)
    img = bytearray(peg_img(0, mode != "stock"))
    inf = bytearray(0x40)
    inf[0x0B] = count
    stock, patched = run_s7_site(
        emus, "PSL1", {UC_X86_REG_EDI: info, UC_X86_REG_EBX: ps}, {ps: bytes(img), info: bytes(inf)}
    )
    assert stock[0][0] == count and stock[0][2] == 0
    exp = want if head_on else count
    assert patched[0][0] == exp and patched[0][1:] == stock[0][1:] and patched[2:] == stock[2:]


def test_psr1_reset_bound_is_the_active_peg_count(emus):
    session, guitar, info, head, ps = (HEAD_MEM + o for o in (0x400, 0x500, 0x600, 0x700, 0))
    s = bytearray(0x40)
    struct.pack_into("<I", s, 0x2C, guitar)
    g = bytearray(0x40)
    struct.pack_into("<I", g, 0x18, head)
    struct.pack_into("<I", g, 0x24, info)
    i = bytearray(0x40)
    i[0x0B] = 6
    h = bytearray(0x40)
    struct.pack_into("<I", h, 0x0C, ps)
    p = bytearray(0x10)
    struct.pack_into("<H", p, 4, 7)
    mem = {session: bytes(s), guitar: bytes(g), info: bytes(i), head: bytes(h), ps: bytes(p)}
    stock, patched = run_s7_site(emus, "PSR1", {UC_X86_REG_ESI: session}, mem)
    assert stock[0][0] == 6 and patched[0][0] == 7
    assert patched[0][2] == stock[0][2] == guitar  # ECX = the guitar, as stock


@pytest.mark.parametrize("index", range(8))
def test_psn1_colour_name(emus, index):
    """Index < 6: the stock table entry; 6/7 (past the table): entry 0, a string-colour name."""
    stock, patched = run_s7_site(
        emus,
        "PSN1",
        {UC_X86_REG_EBX: index, UC_X86_REG_EAX: 0x1111, UC_X86_REG_ESI: S7_MEM},
        {S7_MEM: bytes(0x30)},
    )
    table = struct.unpack("<6I", emus[1].read(0x12ED0C0, 24))
    assert patched[0][2] == table[index if index < 6 else 0]
    if index < 6:
        assert patched[:3] == stock[:3]


@pytest.mark.parametrize("player", [0, 1, 2])
@pytest.mark.parametrize("native", [False, True])
def test_psn1_tracks_player_only_in_owned_pegset(emus, pegs, player, native):
    session, guitar, head, ps = (HEAD_MEM + off for off in (0, 0x100, 0x200, 0x300))
    s, g, h, p = bytearray(0x30), bytearray(0x20), bytearray(0x10), bytearray(0x1D4)
    struct.pack_into("<I", s, 8, player)
    struct.pack_into("<I", s, 0x2C, guitar)
    struct.pack_into("<I", g, 0x18, head)
    struct.pack_into("<I", h, 0x0C, ps)
    p[0x1CE] = 0xA5
    if native:
        own_for(emus, ps, "native")
    stock, patched = run_s7_site(
        emus,
        "PSN1",
        {UC_X86_REG_EBX: 5, UC_X86_REG_ESI: session},
        {session: bytes(s), guitar: bytes(g), head: bytes(h), ps: bytes(p)},
    )
    expected = bytearray(p)
    if native and player < 2:
        expected[0x1CE] = player
    assert patched[:4] == stock[:4]  # register/FPU/stack behaviour remains stock; cold oracle checks flags
    assert patched[4][ps] == bytes(expected)
    for addr in (session, guitar, head):
        assert patched[4][addr] == stock[4][addr]


def test_psn2_null_slot_is_skipped(emus):
    """A NULL slot (an index the PegSet does not hold) returns as `ret 4` would, nothing called (the Wrap entry's before
    callback)."""
    _, patched = emus
    patched.uc.mem_write(S7_ESP, stack(RET_MAGIC, 0))
    patched.run(patched.mid_entries["PSN2"], RET_MAGIC, {**S7_REGS, UC_X86_REG_ESP: S7_ESP, UC_X86_REG_EBX: 6})
    assert patched.uc.reg_read(UC_X86_REG_ESP) == S7_ESP + 8


@pytest.mark.parametrize("index", [0, 5])
def test_psn2_stock_index_reaches_the_dispatch_unchanged(emus, index):
    _, patched = emus
    patched.uc.mem_write(S7_ESP, stack(RET_MAGIC, HEAD_MEM + 0x0C))
    patched.run(
        patched.mid_entries["PSN2"],
        0x59BF80,
        {**S7_REGS, UC_X86_REG_ESP: S7_ESP, UC_X86_REG_EBX: index, UC_X86_REG_EAX: 0x1234},
    )
    # the Wrap entry calls the dispatch on a copy of the slot, below its frame (user, EAX, ECX, EDX)
    esp = patched.uc.reg_read(UC_X86_REG_ESP)
    assert esp == S7_ESP - 24
    assert patched.regs(UC_X86_REG_EAX, UC_X86_REG_EBX) == (0x1234, index)
    assert patched.read(esp + 4, 4) == stack(HEAD_MEM + 0x0C)
    assert patched.read(S7_ESP, 8) == stack(RET_MAGIC, HEAD_MEM + 0x0C)


def run_psl2(emus, img: bytes):
    """Both emulators from the PSL2 call 0x59C60C (ESP -> the pushed PegSet) through the stock
    `mov byte [ebx+8],1; mov eax,[ebp+8]` to 0x59C618, the callee 0x59CBC0 reduced to `ret 4`.
    Returns (stock, patched): EAX, ESP, the PegSet bytes."""
    ps = HEAD_MEM
    out = []
    for emu in emus:
        saved = emu.read(0x59CBC0, 3)
        emu.uc.mem_write(0x59CBC0, bytes.fromhex("C20400"))
        emu.uc.mem_write(ps, img)
        emu.uc.mem_write(S7_EBP + 8, stack(2))
        emu.uc.mem_write(S7_ESP, stack(ps, 0x55AA55AA))
        try:
            emu.run(site_va("PSL2"), 0x59C618, {**S7_REGS, UC_X86_REG_ESP: S7_ESP, UC_X86_REG_EBX: ps})
        finally:
            emu.uc.mem_write(0x59CBC0, saved)
        out.append(
            (
                emu.regs(UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI),
                emu.read(ps, len(img)),
            )
        )
    return out


def test_psl2_is_a_call_site_not_a_jmp_over_0x59c611():
    """0x59C587 `jne 0x59C615` enters 0x59C611's second instruction: PSL2 must leave 0x59C611..0x59C617
    alone (the issue #18 boot crash) and redirect only the call at 0x59C60C."""
    site = next(s for s in sitetable.load(SITES)[1] if s.id == "PSL2")
    assert site.kind == "call" and site_va("PSL2") == 0x59C60C


def test_psl2_completes_the_load_like_stock(emus, pegs):
    """A stock-layout PegSet (no magic): Rs7_PegSetLoaded leaves it alone; the call returns with its
    argument popped, +8 = 1, EAX = [ebp+8]."""
    img = bytearray(0x1D4)
    struct.pack_into("<H", img, 4, 6)
    stock, patched = run_psl2(emus, bytes(img))
    assert patched == stock
    assert stock[0][0] == 2 and stock[0][1] == S7_ESP + 4 and stock[1][8] == 1


def test_psl2_native_pegset_starts_with_six_active(emus, pegs):
    """A native PegSet that loaded 7: active count 6, loaded 7 in the tail (slot 6's entity is culled
    through 0x7AE370; NULL here: nothing to cull)."""
    own_for(emus, HEAD_MEM, "native")
    img = bytearray(0x1D4)
    struct.pack_into("<H", img, 4, 7)
    img[0x1CC] = 8
    struct.pack_into("<I", img, 0x1D0, PEG_MAGIC)
    stock, patched = run_psl2(emus, bytes(img))
    assert patched[0] == stock[0]
    m = patched[1]
    assert struct.unpack_from("<H", m, 4)[0] == 6 and m[0x1CD] == 7 and m[8] == 1
