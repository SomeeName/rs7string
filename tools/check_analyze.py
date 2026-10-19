#!/usr/bin/env python3
"""Check the MSVC static analysis (/analyze) results against tools/analyze_baseline.json.

  uv run python tools/check_analyze.py                   check (build.ps1 runs this after the analysis build)
  uv run python tools/check_analyze.py --write-baseline  drop fixed entries / lower counts in the baseline
  uv run python tools/check_analyze.py --write-baseline --allow-grow   also add new findings (false positives only)

build.ps1 compiles both projects with /p:Rs7Analyze=true (analyze.props: rule set, own object folder obj/analyze),
which leaves one .nativecodeanalysis.xml per source file. A finding is keyed `file::function::Cnnnn` (no line
numbers, so moving code keeps its key) and counted once per line, also when a header is analysed in several
translation units. Same model as tools/check_layout.py: a baseline count may shrink but not grow, a new key
fails, and a key that no longer occurs fails until the baseline is rewritten. A pull request may only shrink the
baseline; every entry carries a note saying why it is a false positive. third_party/ and generated files are not
checked."""

from __future__ import annotations

import argparse
import json
import sys
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BASELINE = REPO / "tools" / "analyze_baseline.json"
RESULTS = REPO / "obj" / "analyze"
PROJECTS = ("rs7string", "rs7string_tests")
_SUFFIX = ".nativecodeanalysis.xml"


class AnalyzeError(Exception):
    """Missing or unreadable analysis results: reported as one line, exit 1."""


def _rel(path: Path) -> str | None:
    """Repository-relative path of a checked source, or None for third-party, generated or foreign files."""
    try:
        rel = path.resolve().relative_to(REPO).as_posix()
    except ValueError:
        return None

    if not rel.startswith(("src/", "tests/")) or "generated" in rel.split("/") or rel.endswith(".generated.h"):
        return None

    return rel if path.exists() else None


def _source_exists(xml: Path, results: Path) -> bool:
    """A result file mirrors its source path (src/features/..., tests/...) or, for the rest of src/, is flat."""
    rel = xml.relative_to(results)
    stem = xml.name.removesuffix(_SUFFIX)

    if len(rel.parts) > 2:
        return (REPO / Path(*rel.parts[1:-1]) / f"{stem}.cpp").exists()

    return any("features" not in p.parts for p in (REPO / "src").rglob(f"{stem}.cpp"))


def _defects(xml: Path):
    try:
        root = ET.parse(xml).getroot()
    except ET.ParseError as e:
        raise AnalyzeError(f"{xml}: not valid XML ({e}); delete obj/analyze and rebuild") from None

    for defect in root.iter("DEFECT"):
        sfa = defect.find("SFA")
        if sfa is None:
            raise AnalyzeError(f"{xml}: a DEFECT without a source location (SFA); delete obj/analyze and rebuild")

        yield defect, sfa


def findings(results: Path | None = None) -> dict[str, int]:
    """Count the findings of every result file whose source still exists (a deleted source leaves a stale file)."""
    results = results or RESULTS

    for project in PROJECTS:
        if not any((results / project).rglob(f"*{_SUFFIX}")):
            raise AnalyzeError(f"no analysis results for {project}; run build.ps1 (it runs the analysis build)")

    seen: set[tuple[str, str, str, str]] = set()

    for xml in sorted(results.rglob(f"*{_SUFFIX}")):
        if not _source_exists(xml, results):
            continue

        for defect, sfa in _defects(xml):
            rel = _rel(Path(sfa.findtext("FILEPATH", "")) / sfa.findtext("FILENAME", ""))
            if rel is None:
                continue

            func = defect.findtext("FUNCTION") or "<file scope>"
            seen.add((rel, func, "C" + defect.findtext("DEFECTCODE", "?"), sfa.findtext("LINE", "?")))

    return dict(sorted(Counter(f"{f}::{fn}::{code}" for f, fn, code, _ in seen).items()))


def compare(current: dict[str, int], baseline: dict[str, dict]) -> list[str]:
    errors = []

    for key, count in current.items():
        if key not in baseline:
            errors.append(f"{key}: {count} new finding(s)")
        elif count > baseline[key]["count"]:
            errors.append(f"{key}: {count} finding(s), grew from {baseline[key]['count']}; the baseline only shrinks")

    for key in sorted(set(baseline) - set(current)):
        errors.append(f"{key}: fixed - remove it from the baseline (--write-baseline)")

    for key, entry in sorted(baseline.items()):
        if not entry.get("note"):
            errors.append(f"{key}: baseline entry without a note (why it is a false positive)")

    return errors


def rewrite(current: dict[str, int], baseline: dict[str, dict], allow_grow: bool) -> tuple[dict, list[str]]:
    """The new baseline (notes kept) and the growth it would need; growth is refused unless allow_grow."""
    grown = [k for k, n in current.items() if k not in baseline or n > baseline[k]["count"]]
    if grown and not allow_grow:
        return baseline, grown

    out = {k: {"count": n, "note": baseline.get(k, {}).get("note", "")} for k, n in current.items()}

    return out, []


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--write-baseline", action="store_true", help="record the current findings")
    ap.add_argument("--allow-grow", action="store_true", help="with --write-baseline: also add or raise entries")
    args = ap.parse_args()
    baseline = json.loads(BASELINE.read_text(encoding="utf-8"))

    try:
        current = findings()
    except AnalyzeError as e:
        print(f"check_analyze: {e}")
        return 1

    if args.write_baseline:
        new, grown = rewrite(current, baseline, args.allow_grow)
        for key in grown:
            print(f"{key}: would grow the baseline (fix it, or pass --allow-grow for a false positive)")

        if grown:
            return 1

        BASELINE.write_text(json.dumps(new, indent=2) + "\n", encoding="utf-8", newline="\n")
        print(f"{BASELINE.name}: {len(new)} key(s), {sum(current.values())} finding(s); add a note to new keys")
        return 0

    errors = compare(current, baseline)
    for e in errors:
        print(e)

    if errors:
        print(f"check_analyze: {len(errors)} problem(s); details in obj/analyze/**/*{_SUFFIX}")
        return 1

    print(f"check_analyze: ok ({sum(current.values())} baselined finding(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
