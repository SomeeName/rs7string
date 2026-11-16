# 13. Known limitations and open risks (1.0)

Part of the [rs7string architecture](../ARCHITECTURE.md).


> [!NOTE]
> Collected in the 1.0 reviews (October and November 2026). Items marked "open" had no fix then; they were not
> all re-checked for 1.0.0-beta.4.

## At a glance

| Area | What remains open or limited |
|---|---|
| Patching and lifetime | Each write round runs with the game's other threads frozen, and a timed-out freeze writes nothing (`src/platform/isolate.cpp`); a rollback that cannot restore every site is logged as `UNRECOVERABLE` and stops patching. Open: whether the locked writes are enough for code another thread is executing ([plain PSARC loading](10-plain-psarc-loading.md)). Song data and render publication may outlive immediate song-store reclamation. |
| Memory | Edited-headstock holders can lose retained references after frame replacement; a faulting ramp path cannot release local COM ownership. These leaks are small and occur per guitar or ramp fault. |
| Settings and input | Overlay editing narrows text to ASCII. Table settings fail closed; a few debug switches read directly treat any nonzero value as on (documented). While the overlay uses the mouse or keyboard, that input is masked from the game ([chapter 11](11-window-input-overlay-logging.md)). |
| Tuner and UI | Names are English only; filter and sort use six-string offsets. A rare `GridBuild` timeout keeps the neck marked failed until a six-string selection. |
| Verification | Overlay device Reset is unit-tested but had not been hit in game. Real-guitar bends, slides and harmonics on string 6 and chord techniques still need a final check. |
| Very low tunings | Some subharmonic-table reads below MIDI 22 are unchecked; see below. Open strings below G0 are undetectable. |

## Settings outside the documented range

Settings in the settings tables fail closed (`src/platform/settings.cpp`, `Rs7Config_ReadInt`): a `Bool` or `Int`
value that is not an integer or lies outside its range reads as 0 (off) and is logged; an invalid `Float`, `Text` or
colour value uses the row's default. The remaining direct `GetPrivateProfileIntW` reads are debug and diagnostic keys
(`[debug] veh_log`, `spin_probe`, `publish_trace`, `log_redact`, `[window] console_quickedit_off`, where any
nonzero value counts as on, and `[debug] plain_delay_ms`, capped at 30000), as
[SETTINGS.md](../release/SETTINGS.md#how-settings-work) documents.

## Low-tuning subharmonic table

The subharmonic table at `0x119A398` has rows for MIDI 22–93 only (`[m, m - 19, ...]`). Most readers
check the range, but `0x4DFD72`, `0x4E05D2`, `0x4E6FF7` and `0x4E7D11` index it with an unchecked
detected note. A peak at MIDI 19–21 reads stock default tunings stored before the table; row 21 is
`{38, 43}`. RSMods' floor of 18 has the same exposure.

**HYPOTHESIS:** a low note can suppress D2 / G2, and for 19 / 20 other notes, played at the same
time. This was not seen in the chord charts. Open strings below G0 (19) remain undetectable because
of the lag buffer.
