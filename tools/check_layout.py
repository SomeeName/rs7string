#!/usr/bin/env python3
"""Check the source layout rules of AGENTS.md "Code structure": file size, function size, inline assembly and
the include direction between src/platform, src/model and src/features/<name>.

  uv run python tools/check_layout.py                   check (build.ps1 runs this)
  uv run python tools/check_layout.py --write-baseline  record the current violations in tools/layout_baseline.json

The baseline lists the violations that existed when a rule was introduced. A listed file or function may shrink but
not grow, a new violation fails, and an entry that no longer violates fails until the baseline is rewritten. The
baseline may only shrink in a pull request (a renamed file or function keeps its entry under the new name)."""

from __future__ import annotations

import argparse
import ast
import json
import re
import sys
from pathlib import Path

import tree_sitter_cpp
from tree_sitter import Language, Node, Parser

REPO = Path(__file__).resolve().parents[1]
BASELINE = REPO / "tools" / "layout_baseline.json"
MAX_FILE_LINES = 800
MAX_FUNCTION_LINES = 100

_PARSER = Parser(Language(tree_sitter_cpp.language()))
_INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
_ASM_BLOCK = re.compile(rb"__asm\s*\{[^{}]*\}")


def _rel(p: Path) -> str:
    return p.relative_to(REPO).as_posix()


def cpp_files() -> list[Path]:
    out = []

    for root in ("src", "tests", "fuzz"):
        for p in sorted((REPO / root).rglob("*")):
            if p.suffix in (".cpp", ".h") and not p.name.endswith(".generated.h") and "generated" not in p.parts:
                out.append(p)

    return out


def py_files() -> list[Path]:
    return sorted(p for p in (REPO / "tools").rglob("*.py") if "__pycache__" not in p.parts)


def file_sizes(files: list[Path]) -> dict[str, int]:
    sizes = {_rel(p): len(p.read_text(encoding="utf-8", errors="replace").splitlines()) for p in files}
    return {k: v for k, v in sizes.items() if v > MAX_FILE_LINES}


def _cpp_name(node: Node) -> str:
    decl = node.child_by_field_name("declarator")

    while decl is not None and decl.type not in ("function_declarator",):
        decl = decl.child_by_field_name("declarator")

    name = decl.child_by_field_name("declarator") if decl is not None else None
    return name.text.decode(errors="replace") if name is not None else f"<line {node.start_point[0] + 1}>"


def mask_asm_blocks(source: bytes) -> bytes:
    """Blank every `__asm { ... }` block but keep its line breaks. tree-sitter cannot parse MSVC inline assembly
    (labels, `0Ch` literals); its error recovery let the enclosing function run on over the next functions."""
    return _ASM_BLOCK.sub(lambda m: re.sub(rb"[^\n]", b" ", m.group()), source)


def cpp_functions(path: Path) -> dict[str, int]:
    tree = _PARSER.parse(mask_asm_blocks(path.read_bytes()))
    found: dict[str, int] = {}
    stack = [tree.root_node]

    while stack:
        node = stack.pop()
        if node.type == "function_definition":
            lines = node.end_point[0] - node.start_point[0] + 1
            name = _cpp_name(node)
            found[name] = max(found.get(name, 0), lines)
            continue

        stack.extend(node.children)

    return found


def py_functions(path: Path) -> dict[str, int]:
    found: dict[str, int] = {}

    for node in ast.walk(ast.parse(path.read_text(encoding="utf-8"))):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            lines = node.end_lineno - node.lineno + 1
            found[node.name] = max(found.get(node.name, 0), lines)

    return found


def function_sizes(cpp: list[Path], py: list[Path]) -> dict[str, int]:
    out = {}

    for p, sizes in [(p, cpp_functions(p)) for p in cpp] + [(p, py_functions(p)) for p in py]:
        for name, lines in sizes.items():
            if lines > MAX_FUNCTION_LINES:
                out[f"{_rel(p)}::{name}"] = lines

    return out


def asm_counts(files: list[Path]) -> dict[str, int]:
    counts = {_rel(p): p.read_text(encoding="utf-8", errors="replace").count("__asm") for p in files}
    return {k: v for k, v in counts.items() if v}


def layer(include: str) -> tuple[str, str]:
    """(layer, feature) of a path relative to src/: platform, model, feature, generated, app or unknown."""
    parts = include.split("/")

    if parts[0] in ("platform", "model", "generated"):
        return parts[0], ""

    if parts[0] == "features" and len(parts) > 2:
        return "feature", parts[1]

    if parts[0] in ("d3d", "overlay") or len(parts) == 1:
        return "app", ""

    return "unknown", ""


# What each layer may include (AGENTS.md "Code structure"). Other features are checked separately: public header only.
# "generated" tables come from tools/gen_*.py.
ALLOWED = {
    "platform": {"platform"},
    "model": {"model", "platform"},
    "feature": {"feature", "platform", "model", "generated"},
    "app": {"feature", "platform", "model", "generated", "app"},
}
NAMES = {"app": "app layer", "feature": "feature", "model": "model", "generated": "generated"}


def include_errors(files: list[Path]) -> list[str]:
    errors = []

    for p in files:
        if p.parts[len(REPO.parts)] != "src":
            continue

        src = p.relative_to(REPO / "src").as_posix()
        own, own_feature = layer(src)

        if own == "unknown":
            errors.append(f"{src}: in no layer (src/platform, model, features/<name>, d3d, overlay or src/*)")
            continue

        for inc in _INCLUDE.findall(p.read_text(encoding="utf-8", errors="replace")):
            to, feature = layer(inc)
            public = to == "feature" and inc == f"features/{feature}/{feature}.h"

            if to != "unknown" and to not in ALLOWED[own]:  # unknown: third-party headers
                allowed = ", ".join(sorted(ALLOWED[own] - {"generated"}))
                errors.append(f"{src}: {own} includes {inc} ({NAMES[to]}); {own} may include {allowed} only")
            elif to == "feature" and feature != own_feature and not public:
                errors.append(
                    f"{src}: includes {inc}, an internal of feature {feature} (use features/{feature}/{feature}.h)"
                )

    return errors


def measure() -> dict[str, dict[str, int]]:
    cpp, py = cpp_files(), py_files()
    return {"file_lines": file_sizes(cpp + py), "function_lines": function_sizes(cpp, py), "asm": asm_counts(cpp)}


def compare(current: dict[str, dict[str, int]], baseline: dict[str, dict[str, int]]) -> list[str]:
    limits = {"file_lines": f"over {MAX_FILE_LINES} lines", "function_lines": f"over {MAX_FUNCTION_LINES} lines"}
    errors = []

    for rule, found in current.items():
        known = baseline.get(rule, {})
        what = limits.get(rule, "inline assembly (__asm)")

        for key, value in sorted(found.items()):
            if key not in known:
                errors.append(f"{key}: {what} ({value}) - new violation")
            elif value > known[key]:
                errors.append(f"{key}: {what} ({value}) - grew from {known[key]}, the baseline only shrinks")

        for key in sorted(set(known) - set(found)):
            errors.append(f"{key}: fixed - remove it from the baseline (--write-baseline)")

    return errors


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--write-baseline", action="store_true", help="record the current violations")
    args = ap.parse_args()

    current = measure()
    if args.write_baseline:
        BASELINE.write_text(json.dumps(current, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
        print(f"{BASELINE.name}: " + ", ".join(f"{len(v)} {k}" for k, v in current.items()))
        return 0

    errors = include_errors(cpp_files()) + compare(current, json.loads(BASELINE.read_text(encoding="utf-8")))
    for e in errors:
        print(e)

    if errors:
        print(f"check_layout: {len(errors)} problem(s), see AGENTS.md 'Code structure'")
        return 1

    print("check_layout: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
