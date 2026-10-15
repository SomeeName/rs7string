#include <windows.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <new>
bool failNext = false;
void* operator new(size_t n) {
    if (failNext) { failNext = false; throw std::bad_alloc(); }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
#include "reader.inc"
int main() {
    wchar_t dir[MAX_PATH]{}, name[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, dir) || !GetTempFileNameW(dir, L"rs7", 0, name)) return 2;
    const std::wstring path(name);
    HANDLE file = CreateFileW(name, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written = 0;
    if (file == INVALID_HANDLE_VALUE || !WriteFile(file, "test", 4, &written, nullptr)) return 3;
    CloseHandle(file);
    std::vector<uint8_t> bytes;
    failNext = true;
    bool threw = false, accepted = false;
    try { accepted = ReadFileBytes(path, bytes); } catch (...) { threw = true; }
    // Exclusive reopen also verifies the read handle was released on the failure path.
    file = CreateFileW(name, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    const bool closed = file != INVALID_HANDLE_VALUE;
    if (closed) CloseHandle(file);
    DeleteFileW(name);
    return !threw && !accepted && closed && bytes.empty() && !failNext ? 0 : 1;
}
