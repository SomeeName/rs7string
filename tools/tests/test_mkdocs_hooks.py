"""The documentation staging hook supports live preview without rebuilding forever."""

import os

import mkdocs_hooks


def test_staging_does_not_rewrite_unchanged_files(tmp_path):
    (tmp_path / "docs").mkdir()
    (tmp_path / "README.md").write_text("Home", encoding="utf-8")
    (tmp_path / "docs" / "guide.md").write_text("Guide", encoding="utf-8")

    staged = mkdocs_hooks.stage(tmp_path)
    guide = staged / "docs" / "guide.md"
    # Give the staged copy a distinctive timestamp. Recopying it restores the source timestamp.
    os.utime(guide, ns=(1_000_000_000, 1_000_000_000))
    before = guide.stat().st_mtime_ns

    mkdocs_hooks.stage(tmp_path)

    assert guide.read_text(encoding="utf-8") == "Guide"
    assert guide.stat().st_mtime_ns == before


def test_staging_updates_and_removes_changed_or_private_files(tmp_path):
    docs = tmp_path / "docs"
    docs.mkdir()
    (tmp_path / "README.md").write_text("Home", encoding="utf-8")
    guide = docs / "guide.md"
    guide.write_text("First", encoding="utf-8")
    private = docs / "private.md"
    private.write_text("Private", encoding="utf-8")

    staged = mkdocs_hooks.stage(tmp_path)
    guide.write_text("Other", encoding="utf-8")  # Same length must still be detected.
    (tmp_path / ".publicignore").write_text("docs/private.md\n", encoding="utf-8")
    (docs / "new.md").write_text("New", encoding="utf-8")

    mkdocs_hooks.stage(tmp_path)

    assert (staged / "docs" / "guide.md").read_text(encoding="utf-8") == "Other"
    assert (staged / "docs" / "new.md").read_text(encoding="utf-8") == "New"
    assert not (staged / "docs" / "private.md").exists()

    guide.unlink()
    mkdocs_hooks.stage(tmp_path)
    assert not (staged / "docs" / "guide.md").exists()


def test_the_github_link_bar_is_left_out_on_the_site():
    page = (
        "# Install\n\n**Install it.**\n\n[README](../../README.md) · **Install** · [Settings](SETTINGS.md)\n\nText.\n"
    )
    assert mkdocs_hooks.strip_nav_bar(page) == "# Install\n\n**Install it.**\n\nText.\n"


def test_other_lines_with_links_stay():
    page = "# T\n\nSee [A](a.md) · and [B](b.md) for more.\n\n- [A](a.md) · [B](b.md)\n"
    assert mkdocs_hooks.strip_nav_bar(page) == page
