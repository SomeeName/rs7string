#pragma once
#include "platform/midhook.h"
#include <cstdint>

// Issue #13 diagnostic: which route ends a song ("the song goes to the results screen ~10 s after
// start"). : the song session's state word
// +390h (3 = playing, 4 = end) goes 3 -> 4 in exactly three ways, each hooked here (log only, the
// stock code runs unchanged):
//   SE1 0x7DD600 Play_Session_OnEndSongEvent entry (ECX = session, [esp+4] = event {target, type,
//       flags}): an end-song event (type 3, target 2 or the session's id +8) - logged with the
//       sender's backtrace.
//   SE2 0x7DFA30 the leave-state call (ESI = session, [esp+4] = the state being left; callers
//       0x7E0A37 UpdateState pending request (new state EDI), 0x7DD659 the end event (new 4),
//       0x7DF855 set state (EDI), 0x7EF067 / 0x7EF0C3 multiplayer sync (EBX)): every leave is one
//       short line, leaving state 3 adds the backtrace.
//   SE3 0x7E0AE6 in Play_Session_UpdateState: state 3 with the song object +78h == NULL.
// [debug] song_end_log=1 (default, live): on. A fault in the logger switches it off for the run.

extern "C" __declspec(dllexport) uintptr_t g_rs7_endlog;  // 0 = the stubs skip the call

// Stub entry (stubs.cpp, register-file pattern of the vguitar stubs): site 1/2/3 = SE1/SE2/SE3;
// f = the stub's pushad block: f[0] EDI, f[1] ESI, f[2] EBP, f[3] ESP, f[4] EBX, f[5] EDX, f[6] ECX,
// f[7] EAX, f[8] EFLAGS, f[9..] the site's stack ([esp], [esp+4], ...).
extern "C" __declspec(dllexport) void __cdecl Rs7End_OnSite(uint32_t site, const uint32_t* f);
// Context-native entry: scan the original game stack, never the callback stack.
extern "C" __declspec(dllexport) void __cdecl Rs7End_OnContext(uint32_t site, const Rs7Mid::Context& context);

namespace rs7end {
// An end-song event as Play_Session_OnEndSongEvent accepts it: type 3, target 2 (all) or sessionId.
bool IsEndEvent(uint32_t target, uint32_t type, uint32_t sessionId);
// The new state at a known caller of 0x7DFA30 (its return address), -1 for an unknown caller.
int NewState(uint32_t ret, uint32_t edi, uint32_t ebx);
// Short name of a known caller of 0x7DFA30 ("" for others).
const char* CallerName(uint32_t ret);
// Whether the 7 bytes before a return address end in a call instruction (E8 rel32, FF /2 with a
// register, [reg], [reg+disp8], [reg+disp32] or [disp32] operand); b[6] is the byte just before it.
bool EndsInCall(const uint8_t b[7]);
// Number of state leaves / end lines logged so far (tests, overlay).
unsigned Lines();
}  // namespace rs7end

// [debug] song_end_log=0|1  1 (default): log which route ends a song (issue #13).
struct Rs7HookTable;
extern const Rs7HookTable kRs7SongEndHooks;  // SE1-SE3, group Core
struct Rs7SetTable;
extern const Rs7SetTable kRs7SongEndSettings;
// The song end log in the Debug tab (platform/uilayout.h).
struct Rs7UiTable;
extern const Rs7UiTable kRs7SongEndUi;
bool Rs7SongEnd_ReadIni(const wchar_t* iniPath);
bool Rs7SongEnd_Config();
