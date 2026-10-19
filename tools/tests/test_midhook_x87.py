"""Old-vs-new equivalence of the sites that need the x87 accessors or the jmp_atomic8 kind (ARCHITECTURE.md section 15,
"x87 ST0" and "Atomic sites"): R3a/R3b/R3c and R4 (string-6 lane position of spawned note effects, full mode, ST0 via
Context::PushSt0) and S3 / PT1 (the two exits of Play_Detector_SetTuning, jmp_atomic8 sites). Frozen stubs:
tools/tests/fixtures/midhook_318618f.json. Same oracle and matrix as test_midhook_cold.py (midhook_oracle.py).
"""

from __future__ import annotations

import itertools
import struct

import pytest

from emu_harness import DLL, SNAPSHOT, DATA, site_va
from midhook_oracle import Case, Helper, Spec, oracle_tests

from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESI

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)

D = DATA + 0x2000
SPECS: dict[str, tuple[Spec, list[Case]]] = {}


def blob(n: int, seed: int) -> bytes:
    return bytes((i * 31 + seed) & 255 for i in range(n))


# ---- R3a/R3b/R3c/R4: `lea eax,[ecx+eax+3Ch]; (pop esi;) fld [eax]`, ECX = string * 4Ch -----------------------------
# Cells: string 0 at EAX + 3Ch, string 1 one 4Ch row further. The value sets include inexact results (the x87 flags
# differ there: contract "sticky") and a float overflow (both give infinity).
CELLS = D + 0x100
ROWS = 0x4C
LANE_VALUES = (
    ((1.5, -2.25, 3.0), (0.75, 0.5, -1.0)),
    ((0.1, 0.2, 0.3), (0.7, -0.11, 1e-3)),
    ((3e38, -3e38, 1e30), (-3e38, 3e38, -1e30)),
    ((-0.0, 0.0, 1e-30), (0.0, -0.0, 3e-30)),
)
STRING_ROWS = (0, ROWS, 5 * ROWS, 6 * ROWS, 7 * ROWS)


def lane_cells(cell0, cell1) -> bytes:
    raw = bytearray(blob(0x300, 9))
    struct.pack_into("<3f", raw, 0x3C, *cell0)
    struct.pack_into("<3f", raw, 0x3C + ROWS, *cell1)
    return bytes(raw)


FULL_STACK = bytes.fromhex("D9 E8") * 6  # 8 values: PushSt0 refuses, the replayed fld overflows (stock and old stub)


def lane_cases(pop: bool) -> list[Case]:
    return [
        Case(
            f"row{row:x}-values{v}-depth{2 + len(x87) // 2}",
            regs={UC_X86_REG_ECX: row, UC_X86_REG_EAX: CELLS},
            mem={CELLS: lane_cells(*LANE_VALUES[v])},
            stack={0: 0xC0FFEE11} if pop else {},
            x87=x87,
        )
        for row, v, x87 in itertools.product(STRING_ROWS, range(len(LANE_VALUES)), (b"", FULL_STACK))
    ]


QUIET = ("Rs7_S7LaneStackFull",)  # the hook's log-once of a full x87 stack
for sid in ("R3a", "R3b", "R3c"):
    SPECS[sid] = (Spec(sid, 6, fsw="sticky", quiet=QUIET), lane_cases(False))
# R4 pops a dword (pushed = -4): the popped slot is below the live stack, scratch for the hook as for any `pop`.
SPECS["R4"] = (Spec("R4", 7, fsw="sticky", pushed=-4, quiet=QUIET), lane_cases(True))

# ---- S3 / PT1: `mov [eax+1356h],cx|dx; ret` (jmp_atomic8, the replay is the whole 8 bytes) ------------------------
# The return address is the site's own `ret` (an exit the run stops at, where the flags are dead: the old stubs left
# the helper's). The helper sees the detector with the store done.
S3_RET, PT1_RET = 0x4E2642, 0x4E26C1  # site_va + 7, constants so that collection needs no game image
DET, VEC = D + 0x1000, D + 0x200
TUNING = {UC_X86_REG_EAX: DET, UC_X86_REG_ECX: 0xABCD1234, UC_X86_REG_EDX: 0x5678FEDC, UC_X86_REG_ESI: VEC}


def exit_cases(ret: int) -> list[Case]:
    return [
        Case(
            f"vec{n}",
            regs=TUNING,
            mem={DET: blob(0x1360, 5), VEC: struct.pack("<3I", D + 0x300, D + 0x300 + 2 * n, D + 0x300 + 16)},
            stack={0: ret, 4: 0x1234},
        )
        for n in (0, 6, 7, 8)
    ]


for sid, helper, ret in (("S3", "Rs7_OnDetectorTuned", S3_RET), ("PT1", "Rs7Pitch_DetTuned", PT1_RET)):
    SPECS[sid] = (
        Spec(sid, 8, flags="dead", helpers={helper: Helper(2, deref=((0, 0x1358), (1, 12)))}, exits=(("resume", -1),)),
        exit_cases(ret),
    )

(
    test_mid_hook_matches_frozen_stub,
    test_dead_flags_are_rewritten_before_any_read,
    test_every_registered_site_calls_its_helper_at_least_once,
) = oracle_tests(SPECS, {"S3", "PT1"})


def test_exit_return_addresses_are_the_sites_ret():
    assert (site_va("S3") + 7, site_va("PT1") + 7) == (S3_RET, PT1_RET)
