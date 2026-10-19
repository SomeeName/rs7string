"""The built DLL image: stub exports are direct (no incremental-link thunks) and the colour-manager reference is bound."""

from __future__ import annotations

import struct

import pefile

from emu_harness import DLL, needs_emu

pytestmark = needs_emu


def test_exports_are_direct(emus):
    """No incremental-link thunks: every stub export is the stub itself (no stub starts with a jmp),
    and the image has no ILT (the linker's thunks are `jmp rel32` runs at the start of .text)."""
    _, patched = emus
    stubs = {n: a for n, a in patched.exports.items() if n.startswith("Rs7_Stub_")}
    # 1.0 legacy removal: - C1-C5 N11 GSCL (7), - the B-phase tuner T1 TA TR M1 M2 L1 L2 TLC TLI TLO1 TLO2 TF PK PE TD (15),
    # - the stage-2 peg PG1-PG4 PH1 PH2 (6), - the S7 object N4 N6 N10 N14 N15 N16 N17 (7), - R7b N9 (2)
    assert (
        len([s for s in stubs if not s.startswith("Rs7_Stub_PUB")])
        == 194 + 5 + 5 + 21 + 3 - 3 - 22 - 7 - 15 - 6 - 7 - 2 - 6 - 5 - 28 - 7 - 1 - 15 - 6 - 66 - 18 - 2 - 2 - 3
    )  # - R2 R6 W (sse pass); - PP_TOC PP_SNGF (mid-hooks of the plain-PSARC unit, asm reduction part 3); - PC1 PC4 (light call-site mid-hooks, asm reduction part 2); - PP1 F1 IN1 PSN2 AM1 TJ TU TN MD1 GIA1 (Wrap mid-hooks), GSA1 GSC1 GRM3 BT1 AG1 AG2 AG3 PE0; - CLBM CLBA CLGU CLGE CLVB NG1 NG9 FGRB DGB DGM DGS TP13 TU8H TU8C TU8A TU8INIT TU8FREE AT 21 TS8* PSU1 PSU2 PSM1 LT1 AL1 TICK JUD1 JUD2 AUD1 GSFR-GSHC GRM1 GRM2 (mid-hooks, step 7 batch 5; + 15 PUB*); - R3a R3b R3c R4 S3 PT1 (x87 ST0 / atomic-site mid-hooks); - TM1-TM6 EV1-EV5 S4 PC2 PC3 PSL2 (call-site mid-hooks, step 7 batch 4); - SF1 (call-site mid-hook); - N5 N18-N20 N22 N23 I1 (batch 2b); - TF1 V2 TP1-TP8 TP11 TP12 DS1 CP1 CP2 LU3 WC2 CS1 PL1 CC3 CH3 UP1-UP3 PD2-PD5 (mid-hooks, step 7 batch 3); - N12 N13 PM0 PR0 PM1 (mid-hooks, refactor step 7 batch 2); - BD1-BD4 D5 B1 (mid-hooks, refactor step 7); TK TS TUT (features/tuner) XB1 N1 N21 GSZ1 GIZ1 PSA2 PSD1 PSD2 PSU3 PSC1 PSL1 PSM2 PSR1 PSN1 GSSS GSSE N2 N3 N7 (cold mid-hook callbacks, mid_cold.cpp); SE1-SE3 context callbacks; + LT1 TF1 IN1 (fix/headstock2: 3+3 letters, tuner framing, inlays); + PP_TOC PP_SNGF PP_SNGD (plain PSARC); + 5 TU8 + 21 TS8 copies; CLBM CLBA CLGU CLGE CLVB; - DGR1 DGR2 (diagfix: chord-diagram movies at the FileOpener vtable) + FGRB (native FretGrid, issue #18 stage 5) + PM0 PR0 PT1 PM1 PE0 PC1-PC4 (native_pitch, issue #18 stage 5 area 3) + NG1 NGP NG9 (native next-note grid, issue #18 stage 5 area 2) + + GSFR-GSHC (16 copied-function entries), GSA1 GSC1 GSZ1 GIA1 GIZ1 GRM1-GRM3 GSSS GSSE GSCL (native GuitarStrings, issue #18 stage 4) + PSA2 PSD1 PSD2 PSU1-PSU3 PSM1 PSM2 (guarded: native PegSets only) + PSC1 PSL1 PSL2 PSR1 PSN1 PSN2 TLC TLI TLO1 TLO2 (native N-slot PegSet) - TP9 TP10 (stale capo frets) + CC3 CH3 RL1 RL2 AG1-AG3 UP1-UP3 PD2-PD5 (native chord tracking) + TP12, PL1 (7th string pluck in chords) - Q1 Q2 E1 Q3a Q3b (native detection: + MD1 LU3 AM1 WC2 EV1-EV5 BT1 CS1) - H1 H2 G1 NT1 FN1 FN3 FK SM1 (native chord rendering) + TM1-TM6, DS1 + TP1-TP11, CP1, CP2, PP1, SF1 (native N-slot chord data, issue #18) + XB1 (RS7X extension block, issue #18) + SE1, SE2, SE3 (song end log) + TJ, TU, TUT, TN (tuning names) + PH1, PH2 (7th knob tint) + TICK, JUD1, JUD2, AUD1 (virtual guitarist), M4 chords: Q1 Q2 Q3a Q3b E1 H1 H2 B1 B2 FN1-FN4 G1 SM1 NT1 FK
    for name, addr in stubs.items():
        assert patched.read(addr, 1)[0] != 0xE9, f"{name} is an ILT thunk"
    pe = pefile.PE(str(DLL), fast_load=True)
    text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    assert text.get_data()[:5][0] != 0xE9 or text.get_data()[5] != 0xE9, ".text starts with an ILT"


# ---- colour manager (the X_CM reference; the stock-colour fallback C1-C5 / N11 went in 1.0) -----

MGR_PTR = 0x0135F58C


def test_colour_manager_ref_is_bound(emus):
    _, patched = emus
    assert struct.unpack("<I", patched.read(patched.exports["g_rs7_colormgr"], 4))[0] == MGR_PTR
