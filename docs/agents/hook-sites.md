# Hook sites and inline assembly

Checklist for adding or removing a hook site, and inline-asm pitfalls. They add detail to [AGENTS.md](../../AGENTS.md).

## Adding or removing a hook site

Keep these consistent in one change:

1. **Site:** `sites.json` (id, a pattern that matches exactly once, `expect` bytes, kind, note), with
   `tools/sitecheck.py` passing.
2. **Hook:** the stub or mid-hook and its registration in
   `src/features/<name>/hooks.cpp` and `registration.cpp` (its hook table, listed once in `kTables` of
   `src/registry.cpp`); a new failure group in `model/groups.h`, its cascade and write conditions in
   `src/hook_rules.cpp`. See [ARCHITECTURE.md section 15](../architecture/15-source-layout.md#15-source-layout).
3. **Settings:** the feature's settings table (`src/features/<name>/settings.cpp`, listed once in `kRs7SettingTables` of
   `src/settings_tables.cpp`), `docs/release/SETTINGS.md`, and the overlay
   controls if it has a key.
4. **Exports:** `src/proxy.def` / `tools/check_exports.py` when a symbol the emulator tests call changes.
5. **Tests:** a native test in `tests/<name>/` (or `tests/test_*.cpp` for platform code) and an emulator comparison against the stock bytes in `tools/tests/`.
6. **Docs:** `docs/ARCHITECTURE.md`, and `docs/release/CHANGELOG.md` for user-visible changes.

Generated headers (`src/generated/`, `src/features/<name>/*.generated.h`) come from
`tools/gen_*.py`. Regenerate them; do not edit them by hand.

## Inline assembly pitfalls

> [!WARNING]
> An inline-asm operand named like a register or mnemonic is not your C variable: `push gs` pushes the
> GS segment register (it once pushed `2Bh`), `push out` pushes nothing useful. Name operands
> `gsPtr`, `outPtr`, never `gs`, `out`, `in`, `ds`, ...

- MSVC warning C4405 reports this class of bug and is an error here (`TreatSpecificWarningsAsErrors`).
  Do not disable it.
- Naked stubs must preserve every register and flag the stock code relies on, including x87/SSE state
  when calling C++ (use the mid-hook thunk for new sites), and must replay the displaced instructions
  exactly. The emulator tests exist to catch deviations.
