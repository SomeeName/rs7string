# Docs style and in-game testing

This page has the style rules for docs, the rules for the docs site, and the rules for testing in the game. It adds detail to [AGENTS.md](../../AGENTS.md).

## Docs style

All docs use plain language, because many readers are not native English speakers.

- Put the main point first in every section, and the details after it.
- Write short sentences (under 20-25 words) with one idea each.
- Use active voice, present tense and "you" for the reader.
- Use everyday words, for example "use" instead of "utilize" and "before" instead of "prior to".
- Explain a technical term the first time you use it in player docs.
- Write full sentences with articles and verbs. Avoid telegraph style such as "Mod: loads X; fails: Y".
  Tables and lists may stay short.
- Don't glue clauses together with colons or semicolons. Write two sentences, or join them with "and" or "because".
- Use American spelling in prose (color, behavior, license). Leave key names, file names and code as they are.

Player docs (`README.md`, `docs/release/` except `CHART-TOOLS.md`) also follow these rules:

- Start with a header block (title, one-line purpose, navigation line). Then give simple explanations
  first and details later. Use tables for settings, requirements and known issues, and numbered steps for procedures.
- Use GitHub callouts (`> [!NOTE]`, `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]`) and `<details>` for long or
  advanced parts.
- Use one name per thing: seven-string, low B, CDLC enabler, plain PSARC, headstock builder.
- Do not use internal addresses, CONFIRMED/HYPOTHESIS tags, or personal or workspace paths. Use "C-lite" only
  in developer docs.
- `docs/release/SETTINGS.md` must list exactly the keys and defaults that
  `scripts\package.ps1 -TemplateOnly` prints.

## Docs site

`mkdocs.yml` builds the documentation site (MkDocs Material). `.github/workflows/pages.yml` publishes it, from
the public repository only. `tools/mkdocs_hooks.py` copies `README.md`, `CONTRIBUTING.md`, `AGENTS.md` and
`docs/` into `site-src/` (git-ignored except `.gitkeep`) and leaves out the `.publicignore` paths. This way the
documents keep the same relative links on GitHub and on the site. To preview the site, run
`uv run --group docs mkdocs serve`. To check it, run `uv run --group docs mkdocs build --strict`. The check fails
on a broken relative link or heading anchor.

- Every page must read well on GitHub **and** on the site. Use only Markdown both render: GitHub callouts (the site
  turns them into boxes), `<details>`, tables, fenced code and Mermaid diagrams. Give diagrams a short text
  explanation for readers who cannot view them. Do not use `!!!`/`???` admonitions, content tabs or emoji shortcodes.
- For cards, put `<div class="grid cards" markdown>` around a list (GitHub shows the plain list). `class="github-only"`
  hides a block on the site, for example a contents list that the site's own outline replaces.
- Add a new page to the `nav` under its audience tab: Players, Chart authors or Developers. The styling is in
  `docs/assets/extra.css`.
- Link to repository files outside the docs (such as `data/`) with an absolute GitHub URL.

## In-game testing

Test only with your own legally owned Rocksmith 2014 Remastered (Steam, September 2022 build). Use
plain (unencrypted) PSARC test charts made for RS Extended Range, and work on a copy of the files that you can restore.

- Back up any file in the game folder before you replace it, and never replace one while the game runs.
- Do not ship or post game screenshots, logs with personal paths, models or other results derived
  from the game's data.
- Include relevant `rsxr.log` lines in bug reports.
