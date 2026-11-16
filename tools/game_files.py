"""Where the maintainer-local files outside this repository live: the user's own game executable (original/), the
memory snapshot (rskeys/snapshot) and the research ledgers. Default: the folder that contains the checkout.

RS7_GAME_FILES=<folder> points the tools and tests elsewhere; an empty folder hides them all, which is how CI runs
and how to check that every test needing them skips cleanly:

  .\\scripts\\pytest_nogame.ps1   (sets RS7_GAME_FILES for that call only; the pytest header shows what was found)
"""

from __future__ import annotations

import os
from pathlib import Path


def _workspace() -> Path:
    """The nearest folder above the checkout that holds original/ (or reference/original/), else the parent."""
    checkout = Path(__file__).resolve().parents[1]
    for folder in checkout.parents:
        if (folder / "original").is_dir() or (folder / "reference" / "original").is_dir():
            return folder
    return checkout.parent


def _local(name: str) -> Path:
    """ROOT/name, or ROOT/reference/name in a workspace that keeps its reference files there."""
    nested = ROOT / "reference" / name
    return nested if not (ROOT / name).exists() and nested.exists() else ROOT / name


FROM_ENV = bool(os.environ.get("RS7_GAME_FILES"))
ROOT = Path(os.environ.get("RS7_GAME_FILES") or _workspace())
UNPACKED_EXE = _local("original") / "Rocksmith2014.unpacked.exe"
ANALYSIS_EXE = _local("original") / "Rocksmith2014.analysis.exe"
SNAPSHOT = _local("rskeys") / "snapshot"
SCRIPTS = ROOT / "scripts"  # the workspace's snapshot helpers (not this repository's scripts/)
RESEARCH = ROOT / "research" if (ROOT / "research").exists() else ROOT / "docs" / "research"
