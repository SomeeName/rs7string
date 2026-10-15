"""Write the exhaustive lexical eight-string audit (offline; no game reads).

Run from this checkout: uv run python tools/audit_eight.py
Every literal 7, kSlots, and string-6 equality/capacity special case in DLL sources
is retained, including harmless constants/comments. The companion report explains
which runtime boundaries require ports. Classifications are source observations,
not claims about unexamined executable consumers.
"""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
RX = re.compile(r"\b7\b|\bkSlots\b|\bkNative\b|\[6\]|(?:==|!=|<=|>=|<|>)\s*6\b|\b0x7[Ff]\b")
NEEDS = {
    "mode.cpp": "seven-only side-store activation / string-6 fallback; publish job",
    "tuner.cpp": "legacy tuner/session/string-6 mirror; native_tuner job",
    "tuner.h": "legacy tuner contract; native_tuner job",
    "headstock.cpp": "seventh peg activation/load; eighth assets and peg lifecycle separate",
    "frets.cpp": "seventh-string span extrapolation; publish/native fretgrid job",
    "neck.cpp": "row-6 fallback only; publish/native nextgrid job",
    "chord7.cpp": "string-6 frame scale/feedback helpers; chordloops/native frame job",
    "gsnative.cpp": "capacity eight is present but counts/palette fallbacks need publish review",
}


def classify(path, line):
    code = line.split("//", 1)[0].strip()
    if not code or code.startswith("#include"):
        return "OK for 8", "comment/documented seven-mode contract, not a runtime bound"
    if "Rs7Log(" in code or "Log(" in code or "snprintf" in code:
        return "OK for 8", "diagnostic text/index; runtime limits covered separately"
    if path.name in ("hsbuild.cpp", "dxt.cpp", "color.cpp", "ramp.cpp", "rampstate.cpp"):
        return "OK for 8", "hash/compression/RGB/palette-state constant, not string capacity"
    if "d3d" in path.parts or "overlay" in path.parts or path.name in ("songend.cpp", "songend.h"):
        return "OK for 8", "graphics/API/stack offset or diagnostics, not an instrument maximum"
    if path.name == "tuningname.cpp":
        if "t[7]" in code or "ReadSeven" in code or "Name7" in code or "SevenFor" in code:
            return "NEEDS CHANGE", "seven-only API intentionally retained; TN awaits native eight-array provenance"
        return "OK for 8", "Name8 / manifest hook reads Rs7String7 behind [tuner] names"
    if path.name == "vguit.cpp":
        if any(s in code for s in ("midi[7]", "g_tuningCount == 7", "voices < 7")):
            return "NEEDS CHANGE", "seven-voice chord scheduler/report; single-note helpers are eight-ready"
        return "OK for 8", "eight-gated single-note helpers/counters or explicit six-slot stock projection"
    if path.name in NEEDS:
        return "NEEDS CHANGE", NEEDS[path.name]
    if path.name == "stubs.cpp":
        if re.search(r"add dword ptr \[esp\], 7\b", code):
            return "OK for 8", "seven bytes skipped at a branch join; not a string count"
        if re.search(r"mov.*[,]\s*7\b", code):
            return "NEEDS CHANGE", "legacy seventh-object request; native alternate path required"
        return "OK for 8", "stock/legacy stub boundary; see native-path requirements in report"
    if path.name in ("diag.cpp", "mid_examples.cpp", "string7diag.cpp"):
        return "OK for 8", "diagnostic sample limited to strings 0/6; does not limit gameplay"
    return "OK for 8", "capacity-eight record/native array or explicit stock/legacy contract; see report"


def main():
    rows = ["path\tline\tclassification\treason\tsource"]
    for p in sorted((ROOT / "src").rglob("*")):
        if p.suffix not in (".cpp", ".h") or "generated" in p.parts:
            continue
        for n, line in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
            if not RX.search(line):
                continue
            status, reason = classify(p, line)
            rows.append(f"{p.relative_to(ROOT).as_posix()}\t{n}\t{status}\t{reason}\t{line.strip()}".replace("\r", ""))
    out = ROOT / "docs" / "eight-audit.tsv"
    out.write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"{out}: {len(rows) - 1} occurrences")


if __name__ == "__main__":
    main()
