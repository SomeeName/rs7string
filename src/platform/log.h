#pragma once
#include <intrin.h>
#include <string>
#include <vector>

// Opens (truncates) the log file once; later calls are ignored. With `redact` (the default) every message is
// stripped of the Windows user name, the user profile path and the computer name before it reaches the file, the
// overlay's log panel or a test sink (see RsxrLog_Redact). The names are read once, from the environment.
void RsxrLog_Open(const std::wstring& path, bool redact = true);
// printf-style line with a timestamp, cut to 1023 characters. Thread-safe. Before RsxrLog_Open it reaches only the
// tail and the sink (no file, no redaction).
void RsxrLog(const char* fmt, ...);
// One log line as written: "[sssssss.mmm] msg\n" (exposed for tests).
std::string RsxrLog_Format(unsigned ms, const char* msg);
// Tests: also hand every formatted message (no timestamp, no newline) to `sink`; nullptr = off.
using RsxrLogSink = void (*)(const char* msg);
void RsxrLog_SetSink(RsxrLogSink sink);
// Bytes per line of the log tail (RsxrLog_Tail), terminator included.
constexpr unsigned kRsxrLogTailLen = 240;
// The newest min(max, 200) lines as written (with timestamp and newline, each cut to kRsxrLogTailLen - 1 chars),
// oldest first, into out[0..*got). Returns the number of lines ever logged (a change counter).
unsigned long RsxrLog_Tail(char (*out)[kRsxrLogTailLen], unsigned max, unsigned* got);

// Rate limit for a log line: true for the first `max` calls on a counter, false afterwards (thread-safe; the counter
// keeps counting, so give each limited line its own).
inline bool RsxrLog_Upto(volatile long* counter, long max) {
    return _InterlockedIncrement(counter) <= max;
}

// A once-only log line: true for the first call on a flag (thread-safe), false afterwards; reset the flag to 0 to arm
// it again.
inline bool RsxrLog_Once(volatile long* flag) {
    return _InterlockedExchange(flag, 1) == 0;
}

// What to hide in a log message. A name is listed once per encoding it may show up in (UTF-8, ANSI).
struct RsxrLogRedactor {
    std::vector<std::string> profiles;   // user profile path, e.g. "C:\Users\Name"          -> %USERPROFILE%
    std::vector<std::string> users;      // account name and profile folder name             -> <user>
    std::vector<std::string> computers;  // computer name                                    -> <pc>
};

// `msg` with every profile path, user name and computer name replaced (pure; exposed for tests). One
// left-to-right pass, so a replacement is never matched again. ASCII case-insensitive; '/' equals '\' in paths.
// A profile path must not run on into a longer word. A user or computer name is replaced only as a whole path
// segment (after '\' or '/', before the end, '\', '/', ':', whitespace, a quote or '='), whatever its length: the
// data only appears in paths, and "max 7 strings" must stay readable. Empty names never match.
std::string RsxrLog_Redact(const std::string& msg, const RsxrLogRedactor& r);
// The profile path, user name and computer name of this Windows session (USERPROFILE, USERNAME, COMPUTERNAME).
RsxrLogRedactor RsxrLog_CurrentIdentity();
