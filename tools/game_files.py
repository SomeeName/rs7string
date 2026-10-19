"""Where the maintainer-local files outside this repository live: the user's own game executable (original/), the
memory snapshot (rskeys/snapshot) and the research ledgers. Default: the folder that contains the checkout.

RS7_GAME_FILES=<folder> points the tools and tests elsewhere; an empty folder hides them all, which is how CI runs
and how to check that every test needing them skips cleanly:

  .\\scripts\\pytest_nogame.ps1   (sets RS7_GAME_FILES for that call only; the pytest header shows what was found)
"""

from __future__ import annotations

import os
from pathlib import Path

FROM_ENV = bool(os.environ.get("RS7_GAME_FILES"))
ROOT = Path(os.environ.get("RS7_GAME_FILES") or Path(__file__).resolve().parents[2])
UNPACKED_EXE = ROOT / "original" / "Rocksmith2014.unpacked.exe"
ANALYSIS_EXE = ROOT / "original" / "Rocksmith2014.analysis.exe"
SNAPSHOT = ROOT / "rskeys" / "snapshot"
SCRIPTS = ROOT / "scripts"  # the workspace's snapshot helpers (not this repository's scripts/)
RESEARCH = ROOT / "research"
