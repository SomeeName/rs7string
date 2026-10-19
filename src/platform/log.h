#pragma once
#include <string>
#include <vector>

// Opens (truncates) the log file once; later calls are ignored. With `redact` (the default) every message is
// stripped of the Windows user name, the user profile path and the computer name before it reaches the file, the
// overlay's log panel or a test sink (see Rs7Log_Redact). The names are read once, from the environment.
void Rs7Log_Open(const std::wstring& path, bool redact = true);
// printf-style line with a timestamp; does nothing before Rs7Log_Open. Thread-safe.
void Rs7Log(const char* fmt, ...);
// One log line as written: "[sssssss.mmm] msg\n" (exposed for tests).
std::string Rs7Log_Format(unsigned ms, const char* msg);
// Tests: also hand every formatted message (no timestamp, no newline) to `sink`; nullptr = off.
using Rs7LogSink = void (*)(const char* msg);
void Rs7Log_SetSink(Rs7LogSink sink);
// The newest min(max, 200) lines as written (with timestamp and newline, each cut to 239 chars),
// oldest first, into out[0..*got). Returns the number of lines ever logged (a change counter).
unsigned long Rs7Log_Tail(char (*out)[240], unsigned max, unsigned* got);

// What to hide in a log message. A name is listed once per encoding it may show up in (UTF-8, ANSI).
struct Rs7LogRedactor {
    std::vector<std::string> profiles;   // user profile path, e.g. "C:\Users\Name"          -> %USERPROFILE%
    std::vector<std::string> users;      // account name and profile folder name             -> <user>
    std::vector<std::string> computers;  // computer name                                    -> <pc>
};

// `msg` with every profile path, user name and computer name replaced (pure; exposed for tests). One
// left-to-right pass, so a replacement is never matched again. ASCII case-insensitive; '/' equals '\' in paths.
// A profile path must not run on into a longer word. A user or computer name is replaced only as a whole path
// segment (after '\' or '/', before the end, '\', '/', ':', whitespace, a quote or '='), whatever its length: the
// data only appears in paths, and "max 7 strings" must stay readable. Empty names never match.
std::string Rs7Log_Redact(const std::string& msg, const Rs7LogRedactor& r);
// The profile path, user name and computer name of this Windows session (USERPROFILE, USERNAME, COMPUTERNAME).
Rs7LogRedactor Rs7Log_CurrentIdentity();
