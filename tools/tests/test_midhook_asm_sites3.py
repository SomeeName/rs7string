"""Old-vs-new equivalence and stock references of the sites converted in asm reduction part 3 (stubs frozen at fdfddcf,
tools/tests/fixtures/midhook_fdfddcf.json): the plain-PSARC unit's jmp_atomic8 sites PP_TOC (plain TOC skips the decrypt
block) and PP_SNGF (SNG flags 1 pass the flags check), features/psarc/hooks_mid.cpp, full mode.
What is compared: midhook_oracle.py and midhook_stock.py. PP_SNGF's runs continue through the stock `jne` at site + 5
(the old stub resumed there, the hook replays it or resumes there), so both end after it: at site + 11 or its target.
"""

from __future__ import annotations

import struct

import pytest

from emu_harness import DATA, DLL, SNAPSHOT, game_text, site_va, TEXT_VA
from midhook_oracle import Case, Spec, oracle_tests
from midhook_stock import Ref, W, ruled, stock_tests

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)

D = DATA + 0x2000  # scratch objects
SPECS: dict[str, tuple[Spec, list[Case]]] = {}
TOC_SKIP = 0x4D  # the end of the TOC decrypt block, from the site


def register(spec: Spec, cases: list[Case]):
    SPECS[spec.sid] = (spec, cases)


# ---- PP_TOC: `mov ecx,10h`; [ebp-54h] = archive flags (bit 2 = encrypted TOC), [ebp-74h] = the TOC pointer's slot ----
TOC_PTR, TOC = D + 0x100, D + 0x200

register(
    Spec(
        "PP_TOC",
        5,
        words={"g_rs7_skip_PP_TOC": site_va("PP_TOC") + TOC_SKIP},
        exits=("resume", ("resume", TOC_SKIP - 5)),
    ),
    [
        Case(f"f{flags:x}", ebp={-0x54: flags, -0x74: TOC_PTR}, mem={TOC_PTR: struct.pack("<I", TOC)})
        for flags in (0, 1, 4, 5, 0xFB, 0xFF, 0x104)
    ],
)

# ---- PP_SNGF: `cmp dword [esp+68h],3` before the stock `jne <reject>` ---------------------------------------------------
SNGF = site_va("PP_SNGF")
SNGF_JNE = struct.unpack("<i", game_text()[SNGF + 7 - TEXT_VA : SNGF + 11 - TEXT_VA])[0]

register(
    Spec("PP_SNGF", 5, exits=(("resume", 6), ("resume", 6 + SNGF_JNE))),
    [Case(f"f{flags:x}", stack={0x68: flags}) for flags in (0, 1, 2, 3, 4, 0x101, 0xFFFFFFFF)],
)

ALWAYS_CALLS: set[str] = set()
(
    test_mid_hook_matches_frozen_stub,
    test_dead_flags_are_rewritten_before_any_read,
    test_every_registered_site_calls_its_helper_at_least_once,
) = oracle_tests(SPECS, ALWAYS_CALLS)


def toc_native(case: Case):
    """A plain TOC (bit 2 clear) skips the decrypt block with what its tail sets (EDX = the slot, EDI = the TOC)."""
    if case.ebp[-0x54] & 4:
        return None
    return W("eip", "regs", eip=TOC_SKIP, regs={"edx": TOC_PTR, "edi": TOC})


def sngf_native(case: Case):
    """SNG flags 1 (plain) pass the flags check like 3 (stock: rejected)."""
    if case.stack[0x68] != 1:
        return None
    return W("eip", eip=11)


REFS: dict[str, Ref] = {
    "PP_TOC": Ref(**ruled(toc_native, why="a plain TOC skips the decrypt block")),
    "PP_SNGF": Ref(**ruled(sngf_native, why="plain SNG flags 1 pass the flags check")),
}
(
    test_hook_behaves_like_the_stock_bytes,
    test_hook_differs_from_the_stock_bytes_on_purpose,
    test_every_converted_site_has_a_stock_reference_or_a_reason,
) = stock_tests(SPECS, REFS, {})
