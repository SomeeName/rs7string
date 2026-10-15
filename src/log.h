#pragma once
#include <string>

// Opens (truncates) the log file once; later calls are ignored.
void Rs7Log_Open(const std::wstring& path);
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
