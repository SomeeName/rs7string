#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>
static int failAfter = -1;
void* operator new(size_t n) {
    if (failAfter == 0) { failAfter = -1; throw std::bad_alloc(); }
    if (failAfter > 0) --failAfter;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
#include "core/extblock.cpp"
static int freed = 0, loaded = 0, failureLogs = 0;
static bool liveNs = true, throwLog = false;
void Rs7Log(const char* message, ...) {
    if (std::strstr(message, "load failed")) ++failureLogs;
    if (throwLog) throw std::bad_alloc();
}
void Rs7Ns_OnSngLoaded(void*) { ++loaded; liveNs = true; }
void Rs7Ns_OnSongFree(void*) { ++freed; liveNs = false; }
bool rs7chord::FromTemplateBytes(const uint8_t*, Ext*) { return false; }
const uint8_t example[] = {
'R','S','7','X',1,0,16,0,0x4e,0,0,0,3,0,0,0,
'S','T','R','S',1,0,1,0,5,0,0,0,0,7,1,0,6,
'C','H','D','T',1,0,1,0,8,0,0,0,2,0,0,0,2,1,255,255,
'C','H','N','T',1,0,0,0,13,0,0,0,1,0,0,0,0,32,0,0,255,255,0,0,0};
bool NullInput(rs7ext::Block* out) {
    __try { return rs7ext::Parse(nullptr, 4, 2, 1, 7, out, nullptr, 0) == rs7ext::Status::Invalid; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
int main() {
    rs7ext::Block block;
    if (!NullInput(&block)) { std::puts("FAIL: null nonempty input faults instead of returning Invalid"); return 1; }
    if (rs7ext::Parse(example, sizeof example, 2, 1, 7, nullptr, nullptr, 0) != rs7ext::Status::Invalid) return 2;
    for (size_t size = 1; size < sizeof example; ++size) {
        char why[160];
        if (rs7ext::Parse(example, size, 2, 1, 7, &block, why, sizeof why) == rs7ext::Status::Ok) return 3;
    }
    unsigned accepted = 0, rejected = 0;
    for (size_t at = 0; at < sizeof example; ++at) for (unsigned bit = 0; bit < 8; ++bit) {
        uint8_t bytes[sizeof example]; std::memcpy(bytes, example, sizeof bytes); bytes[at] ^= uint8_t(1u << bit);
        rs7ext::Block out; char why[160];
        auto result = rs7ext::Parse(bytes, sizeof bytes, 2, 1, 7, &out, why, sizeof why);
        if (result == rs7ext::Status::Ok) {
            ++accepted;
            if (out.stringCount != 7 || out.strings.size() != 1 || (out.hasTemplates && out.fret.size() != 2) ||
                out.fret.size() != out.finger.size() || (out.hasChordNotes && out.tech.size() != 1)) return 4;
        } else { ++rejected; if (!why[0]) return 5; }
    }
    if (!accepted || !rejected) return 6;
    // Fail in Parse's first vector allocation after the loader cleared a recycled song's store.
    uint8_t song[0x120] = {}, stream[0x1c] = {}, templates[2 * rs7chord::kTplSize] = {}, tuning[14] = {}, cn[0x948] = {};
    uintptr_t vectors[2] = {reinterpret_cast<uintptr_t>(templates), reinterpret_cast<uintptr_t>(templates + sizeof templates)};
    std::memcpy(song + 0x94, vectors, 8);
    vectors[0] = reinterpret_cast<uintptr_t>(tuning); vectors[1] = vectors[0] + sizeof tuning;
    std::memcpy(song + 0x110, vectors, 8);
    vectors[0] = reinterpret_cast<uintptr_t>(cn); vectors[1] = vectors[0] + sizeof cn;
    std::memcpy(song + 0xac, vectors, 8);
    uintptr_t input = reinterpret_cast<uintptr_t>(example); uint32_t inputSize = sizeof example;
    std::memcpy(stream + 0x10, &input, 4); std::memcpy(stream + 0x18, &inputSize, 4);
    for (int fail : {0, 3}) {
        Rs7Ext_Store(song, std::make_shared<rs7ext::Block>());
        freed = loaded = failureLogs = 0; liveNs = true;
        failAfter = fail; // First copy, or STRS vector allocation after copies/shared Block.
        Rs7Ext_OnSngLoaded(song, stream);
        if (failAfter != -1 || Rs7Ext_Find(song) || freed != 1 || loaded || failureLogs != 1 || liveNs) {
            std::puts("FAIL: loader allocation failure did not clear prior N-slot data and log"); return 7;
        }
    }
    throwLog = true; failAfter = 0;
    Rs7Ext_OnSngLoaded(song, stream); // Even a throwing cleanup logger must stay inside the wrapper.
    throwLog = false;
    std::printf("PASS: all truncations and %u accepted/%u rejected deterministic bit mutations; null inputs rejected\n", accepted,rejected);
}
