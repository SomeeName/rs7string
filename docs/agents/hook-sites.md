# Hook sites and inline assembly

This page has the checklist for adding or removing a hook site, and the pitfalls of inline assembly. It adds detail to [AGENTS.md](../../AGENTS.md).

## Adding or removing a hook site

Update all of these together, in one change:

1. **Site:** `sites.json` (id, a pattern that matches exactly once, `expect` bytes, kind, note), with
   `tools/sitecheck.py` passing.
2. **Hook:** the stub or mid-hook and its registration in
   `src/features/<name>/hooks.cpp` and `registration.cpp`. The feature's hook table is listed once in `kTables` of
   `src/registry.cpp`. A new failure group goes in `model/groups.h`, and its cascade and write conditions go in
   `src/hook_rules.cpp`. See [ARCHITECTURE.md section 15](../architecture/15-source-layout.md#15-source-layout).
3. **Settings:** the feature's settings table (`src/features/<name>/settings.cpp`, listed once in `kRsxrSettingTables` of
   `src/settings_tables.cpp`), `docs/release/SETTINGS.md`, and the overlay
   controls if the feature has a key.
4. **Exports:** `src/proxy.def` / `tools/check_exports.py`, if a symbol that the emulator tests call changes.
5. **Tests:** a native test in `tests/<name>/` (or `tests/test_*.cpp` for platform code) and an emulator comparison against the stock bytes in `tools/tests/`.
6. **Docs:** `docs/ARCHITECTURE.md`, and `docs/release/CHANGELOG.md` for user-visible changes.

Generated headers (`src/generated/`, `src/features/<name>/*.generated.h`) come from
`tools/gen_*.py`. Regenerate them. Do not edit them by hand.

## Inline assembly pitfalls

> [!WARNING]
> If an inline-asm operand has the name of a register or mnemonic, the assembler does not use your C variable.
> `push gs` pushes the GS segment register (it once pushed `2Bh`), and `push out` pushes nothing useful.
> Name operands `gsPtr`, `outPtr`, never `gs`, `out`, `in`, `ds`, ...

- MSVC warning C4405 reports this kind of bug. This project treats it as an error (`TreatSpecificWarningsAsErrors`).
  Do not disable it.
- Naked stubs must preserve every register and flag that the stock code relies on. When a stub calls C++, that
  also covers the x87/SSE state, so use the mid-hook thunk for new sites. Stubs must also replay the displaced
  instructions exactly. The emulator tests are there to catch any difference.
