"""Regression: an MSVC inline-asm operand named like a register is the register, not the C variable.
`push gs` in Rs7_S7Pluck pushed the GS segment selector (0x2B) as the GuitarStrings argument and crashed
the native chord pluck (crash-59ad3c.md). Flag segment-register names used as asm operands."""

import pathlib
import re

SRC = pathlib.Path(__file__).resolve().parents[2] / "src"
SEG = re.compile(r"^\s*(?:__asm\s*)?(?:push|pop|mov|lea|cmp|test|add|sub|call)\b[^;/]*\b(gs|fs|es|ds|cs|ss)\b", re.I)


def test_no_segment_register_operands_in_inline_asm():
    bad = []
    for f in SRC.rglob("*.cpp"):
        in_asm = False
        for n, line in enumerate(f.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            code = line.split("//")[0]
            if "__asm" in code:
                in_asm = True
            if (
                in_asm
                and SEG.match(code)
                and "fs:" not in code.lower()
                and "ptr" not in code.lower().split(",")[0] + "x"
            ):
                bad.append(f"{f.name}:{n}: {line.strip()}")
            if in_asm and "}" in code and "__asm" not in code:
                in_asm = False
    assert not bad, bad
