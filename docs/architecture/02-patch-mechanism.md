# 2. Patch mechanism

Part of the [rs7string architecture](../ARCHITECTURE.md).

The proxy waits until the game has unpacked itself, checks the build, resolves every site, then
writes the patches. A failed core site stops the main batch; other groups can fail on their own.
Plain PSARC loading is a separate unit.

```mermaid
flowchart LR
    start["Game starts<br/>and unpacks"] --> check["Check build<br/>and resolve sites"]
    check --> patch["Apply patch groups<br/>and run hooks"]
```

## Startup

The proxy forwards to the system `dinput8.dll`. A worker thread waits for `d3d9.dll` to load (by
then the exe is unpacked), checks the PE checksum, resolves every site and writes jumps into the
DLL's stubs. The DLL must not import `d3d9.dll`, or it would load before the wait;
`tools/check_exports.py` enforces this.

## Sites

Sites are listed in `sites.json`; `tools/gen_sites.py` turns them into
`src/generated/sites.generated.h` (not committed). A site has an id, a byte `pattern` (with `??`
wildcards) that must match exactly once in `.text`, an `offset`, the `expect`ed stock bytes, a
`note` with its purpose and address, and a kind:

| Kind | Meaning |
|---|---|
| `jmp` | overwrite with a jump into a stub or mid-hook |
| `jmp_atomic8` | the same, written as one 8-byte exchange |
| `call` | redirect the `E8` call at the site |
| `rel32` | rewrite a rel32 operand |
| `bytes` | in-place byte patch |
| `ref` | read the dword at the site (for example a game global) |
| `callref` | read the callee of the `E8` at the site |

Resolution is per feature group: a missing or mismatching site disables its own group before
anything is written (see [Coexistence and safety](#coexistence-and-safety)). Only a core site, or a
member of a unit without a fallback, stops patching altogether (`src/platform/isolate.h`).
`tools/sitecheck.py` re-resolves all sites against an unpacked copy of the game image and checks
that each site covers whole instructions and has no inbound branch.

## Stubs

Stubs (`src/features/<name>/hooks_stubs*.cpp`) are naked functions that replay the displaced stock
instructions. They are checked against the stock code in the Unicorn emulator
(`tools/tests/test_emu*.py`).

## Mid-hooks

Mid-hooks (`src/platform/midhook.*`, CONFIRMED by unit and emulator tests) give a site a plain C++
callback `void __cdecl Callback(Rs7Mid::Context&)` instead of hand-written assembly. One common
thunk (`src/platform/midhook_thunks.asm`) saves GPRs, EFLAGS and x87/SSE state, clears DF, calls the
callback, restores, and returns into a relocated copy of the displaced instructions, which jumps
back to `site + stolen length`.

- `ctx.esp` is the original game ESP; registers and flags can be changed; setting `ctx.jump` skips
  the replay. `Context::St0` / `PushSt0` edit the saved x87 stack.
- The relocator accepts common ALU/MOV/LEA/TEST, register PUSH/POP, immediates, MOVZX/MOVSX/SETcc
  and relative CALL/JMP/Jcc. It accepts one `0x66` operand-size prefix on supported forms, but not
  transfers, x87 or SETcc. It rejects other prefixes, LOOP/JCXZ, indirect transfers, unknown opcodes,
  branches into instruction interiors, internal calls and call/pop. There is no blind-copy fallback.
  Callees that inspect their return address need a hand-written hook
  ([chapter 14](14-mid-hook-contract-and-cold-site-conversion.md)).
- Modes and cost, measured on one machine (microbenchmark, not frame time; CONFIRMED,
  `tests/test_midhook_bench.cpp`, three runs with the guard): full 237-245 ns per call; Light
  (`Rs7Hook::midMode = Rs7Mid::Mode::Light`, no floating-point save, for callbacks without x87/SSE
  code) 50-51 ns; Sse (`Rs7Mid::Mode::Sse`, XMM0-7 and MXCSR only, for SSE float math without
  x87) 55-59 ns.
- Besides `jmp` sites, a mid-hook can sit on a `jmp_atomic8` site or on a `call rel32`
  ([chapter 14, "Call sites"](14-mid-hook-contract-and-cold-site-conversion.md#call-sites)):
  **Before** (at the callee's entry; `Rs7Mid::Ret` / `Return` replace the callee), **After** (after
  the callee returned) and **Wrap** (`Rs7Mid::Call::Wrap`: the callee runs on copies of its arguments
  between an optional before and an after callback that both see the originals,
  ["Wrap placement"](14-mid-hook-contract-and-cold-site-conversion.md#wrap-placement)).
- Live detach is not supported.

D2/D3 in `src/features/diag/hooks.cpp` show the registration pattern. To add a mid-hook: add a
`jmp` site (5..16 bytes, whole instructions), write the callback, add its row to the feature's `registration.cpp` with the
builders in `platform/hooktable.h` (`Rs7_Mid("ID", &Rs7_Mid_ID, &g_rs7_resume_ID)`, then `.Light()` / `.Sse()`,
`.After()`, `.Gate(kRs7Gate...)` from `model/gates.h`), add an emulator comparison and run the gates.

## Copied game functions

Some stock functions are copied byte for byte from the game image, with only array displacements and
count bytes moved to the capacity-8 layouts (`tools/gen_fncopy.py`, `gen_tuner8s.py`,
`gen_publish*.py` -> `src/features/<name>/*generated.h`). Each copy is checked against the live code
at startup (FNV-1a) and against the stock function in the emulator. Where a copy calls into
rs7string (a call edit in place of one stock instruction or call), the target is a call-in entry: a
C++ callback with the copy's registers (`Rs7Mid::CallIn`,
[chapter 14, "Call-in entries"](14-mid-hook-contract-and-cold-site-conversion.md#call-in-entries)),
or one of the few remaining asm helpers.

## Coexistence and safety

- Each write is one locked 8-byte compare-exchange (`InterlockedCompareExchange64`,
  `src/platform/patch.cpp`) on a window inside one cache line. Each site is classified stock / ours /
  foreign; a foreign write (another mod) leaves that group alone and is logged.
- A recoverable startup preparation or write failure disables the site's group and restarts the patch round
  when that group has a fallback ("isolation", `src/platform/isolate.cpp`).
- A handled runtime callback fault restores the saved context and default continuation, then counts
  the fault (CONFIRMED by `tests/test_midhook_guard.cpp`, `Rs7Mid_Guard` in `src/platform/midhook.cpp`).
  Full mode also restores the saved x87 state; SSE mode drops a pending `LoadSt0`. The first 20 faults
  are logged as `hook fault: callback dinput8+<offset>`. Earlier memory writes stay written. The feature
  stays enabled, so later hits call it again; no patch round restarts.
- The native layouts (colours, PegSet, GuitarStrings, FretGrid, pitch, next-note grid, publication,
  tuner, C-lite) form one unit with no fallback (`Rs7_IsSevenUnit`, `src/model/groups.cpp`). If a
  member fails at startup, nothing of the main batch is written and the game stays stock: six-string
  guitar, four-string bass. Plain PSARC is separate. Diagrams, chord loops, the private extras and
  diagnostics are isolated on their own.
- A loss at runtime (the deferred FretGrid write) clears only the switches the stubs read, together
  with its dependants (publication, tuner, C-lite).
- Feature switches live in `rs7string.ini` (see [SETTINGS.md](../release/SETTINGS.md)).
- RSMods hooks d3d9 function bodies, not vtables, so both hook orders chain. RS_ASIO and RSMods were
  tested together with the mod.
