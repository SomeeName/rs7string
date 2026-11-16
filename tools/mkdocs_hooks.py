"""MkDocs hook: stage the documentation into `site-src/` (the folder exists in the repository only as `.gitkeep`, because MkDocs checks docs_dir before it runs hooks) with the repository's own layout.

README.md, CONTRIBUTING.md and AGENTS.md live at the repository root, outside `docs/`, and MkDocs does not allow
`docs_dir` to be the repository root. Staging them next to a copy of `docs/` keeps every relative link exactly as
it is on GitHub, so no document has to be edited for the site. Paths listed in `.publicignore` (private-only
files) are never staged.
"""

import re
import shutil
from pathlib import Path

ROOT_FILES = ["README.md", "CONTRIBUTING.md", "AGENTS.md", "SECURITY.md", "CODE_OF_CONDUCT.md", "NOTICE.md", "LICENSE"]
STAGE = "site-src"


def _private(root: Path) -> list[str]:
    ignore = root / ".publicignore"
    if not ignore.is_file():  # the public repository does not carry the list: nothing private is left in it
        return []
    lines = ignore.read_text(encoding="utf-8").splitlines()
    return [ln.strip().rstrip("/") for ln in lines if ln.strip() and not ln.startswith("#")]


def stage(root: Path) -> Path:
    """Sync the public documentation into root/site-src; leave unchanged files alone."""
    out = root / STAGE
    out.mkdir(exist_ok=True)
    private = _private(root)

    def is_private(rel: str) -> bool:
        return any(rel == p or rel.startswith(p + "/") for p in private)

    sources = {}
    for name in ROOT_FILES:
        if (root / name).is_file() and not is_private(name):
            sources[name] = root / name
    for src in sorted((root / "docs").rglob("*")):
        rel = src.relative_to(root).as_posix()
        if src.is_file() and not is_private(rel):
            sources[rel] = src

    # Remove obsolete and newly private files, but preserve the tracked root placeholder.
    entries = sorted(out.rglob("*"), key=lambda path: len(path.parts), reverse=True)
    for old in entries:
        if old.is_symlink():
            old.unlink()
        elif old.is_file() and old.relative_to(out).as_posix() not in sources:
            if old != out / ".gitkeep":
                old.unlink()
        elif old.is_dir() and not any(old.iterdir()):
            old.rmdir()

    for rel, src in sources.items():
        dst = out / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        if not dst.is_file() or src.read_bytes() != dst.read_bytes():
            shutil.copy2(src, dst)
    return out


def on_config(config, **kwargs):
    stage(Path(config["config_file_path"]).parent)
    return config


def on_serve(server, config, **kwargs):
    """Rebuild the staged copy when a source document changes."""
    root = Path(config["config_file_path"]).parent
    # MkDocs watches docs_dir by default. It is generated here, so watching it as well
    # would trigger a second build after staging a source edit.
    server.unwatch(config["docs_dir"])
    for source in [root / "docs", *(root / name for name in ROOT_FILES), root / ".publicignore"]:
        if source.exists():
            server.watch(str(source))
    return server


# The line of page links under each title ("[README](...) · **Install** · [Settings](...)") is navigation for
# GitHub readers. The site has tabs and a sidebar, so the site leaves that line out.
_LINK = r"(?:\[[^\]\n]+\]\([^)\s]+\)|\*\*[^*\n]+\*\*)"
_NAV_BAR = re.compile(rf"^{_LINK}(?: · {_LINK})+[ \t\r]*$")


def strip_nav_bar(markdown: str) -> str:
    """Drop the first link bar within the first ten lines; everything else stays as it is."""
    lines = markdown.split("\n")
    for i, line in enumerate(lines[:10]):
        if _NAV_BAR.match(line):
            del lines[i]
            if i < len(lines) and not lines[i].strip() and i > 0 and not lines[i - 1].strip():
                del lines[i]
            break
    return "\n".join(lines)


def on_page_markdown(markdown, **kwargs):
    return strip_nav_bar(markdown)
