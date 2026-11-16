"""package.ps1: release docs in the zip link to the docs site, never to repository files the zip does not carry."""

import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SHELL = shutil.which("pwsh") or shutil.which("powershell")
SITE = (
    re.search(r"^site_url:\s*(\S+)", (ROOT / "mkdocs.yml").read_text(encoding="utf-8"), re.M).group(1).rstrip("/") + "/"
)
DOCS = sorted((ROOT / "docs/release").glob("*.md"))


def zip_text(doc: Path) -> str:
    rel = doc.relative_to(ROOT).as_posix()
    cmd = [SHELL, "-NoProfile", "-File", str(ROOT / "scripts/package.ps1"), "-ReleaseDoc", rel]
    return subprocess.run(cmd, check=True, capture_output=True, text=True, encoding="utf-8").stdout


@pytest.mark.skipif(not SHELL, reason="needs PowerShell")
@pytest.mark.parametrize("doc", DOCS, ids=lambda d: d.name)
def test_release_doc_links_stay_in_the_zip_or_go_to_the_site(doc):
    text = zip_text(doc)
    assert "](../" not in text
    for url in re.findall(r"\]\((https://[^)\s]+)\)", text):
        if url.startswith(SITE):
            path = url[len(SITE) :].split("#")[0]
            assert path == "" or path.endswith("/"), url
    assert text.count("](") == doc.read_text(encoding="utf-8").count("](")


@pytest.mark.skipif(not SHELL, reason="needs PowerShell")
def test_readme_and_developer_links_map_to_site_pages():
    text = zip_text(ROOT / "docs/release/INSTALL.md")
    assert f"]({SITE})" in text  # ../../README.md is the site's home page
    assert "](../../README.md)" not in text
