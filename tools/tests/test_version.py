"""RS7_VERSION must name the newest changelog release, so a build never shows a stale version."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def test_version_matches_newest_changelog_release():
    version = re.search(r'RS7_VERSION "([^"]+)"', (ROOT / "src/version.h").read_text()).group(1)
    changelog = (ROOT / "docs/release/CHANGELOG.md").read_text(encoding="utf-8")
    newest = re.search(r"^## (\d[^\s]*)$", changelog, re.M).group(1)
    assert version.removesuffix("-dev") == newest
