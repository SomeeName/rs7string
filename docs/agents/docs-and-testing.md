# Docs style and in-game testing

Style rules for player docs, the docs site, and the rules for testing in the game. They add detail to [AGENTS.md](../../AGENTS.md).

## Docs style

Player docs (`README.md`, `docs/release/` except `RS7X-SPEC.md`, `CHART-TOOLS.md`):

- A header block (title, one-line purpose, navigation line), then simple explanations first and
  details later. Tables for settings, requirements and known issues; numbered steps for procedures.
- GitHub callouts (`> [!NOTE]`, `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]`) and `<details>` for long or
  advanced parts.
- Terms: seven-string, low B, CDLC enabler, plain PSARC, headstock builder.
- No internal addresses, no CONFIRMED/HYPOTHESIS tags, no personal or workspace paths. "C-lite" only
  in developer docs.
- `docs/release/SETTINGS.md` must list exactly the keys and defaults that
  `scripts\package.ps1 -TemplateOnly` prints.

## Docs site

`mkdocs.yml` builds the documentation site (MkDocs Material); `.github/workflows/pages.yml` publishes it from
the public repository only. `tools/mkdocs_hooks.py` stages `README.md`, `CONTRIBUTING.md`, `AGENTS.md` and
`docs/` into `site-src/` (git-ignored except `.gitkeep`) without the `.publicignore` paths, so the documents keep
the same relative links on GitHub and on the site. Preview with `uv run --group docs mkdocs serve`; check with
`uv run --group docs mkdocs build --strict`, which fails on a broken relative link or heading anchor.

- Every page must read well on GitHub **and** on the site. Use only Markdown both render: GitHub callouts (the site
  turns them into boxes), `<details>`, tables, fenced code and Mermaid diagrams. Give diagrams a short text
  explanation for readers who cannot view them. No `!!!`/`???` admonitions, content tabs or emoji shortcodes.
- Cards: `<div class="grid cards" markdown>` around a list (GitHub shows the list). `class="github-only"` hides a
  block on the site, for example a contents list the site's own outline replaces.
- A new page goes into the `nav` under its audience tab: Players, Chart authors or Developers. Styling lives in
  `docs/assets/extra.css`.
- Link to repository files outside the docs (such as `data/`) with an absolute GitHub URL.

## In-game testing

Test only with your own legally owned Rocksmith 2014 Remastered (Steam, September 2022 build), with
plain (unencrypted) PSARC test charts made for rs7string, on a copy of the files you can restore.

- Back up any file in the game folder before you replace it, and never replace one while the game runs.
- Do not ship or post game screenshots, logs with personal paths, models or other results derived
  from the game's data.
- Include relevant `rs7string.log` lines in bug reports.
