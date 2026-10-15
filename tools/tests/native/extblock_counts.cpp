#include <windows.h>
#include <cstdlib>
#include <new>
#include <cstdio>
static bool oversized = false;
void* operator new(size_t n) {
    if (n > (1u << 20)) { oversized = true; throw std::bad_alloc(); }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
#include "core/extblock.cpp"
void Rs7Log(const char*, ...) {}
void Rs7Ns_OnSngLoaded(void*) {}
void Rs7Ns_OnSongFree(void*) {}
bool rs7chord::FromTemplateBytes(const uint8_t*, Ext*) { return false; }
void U32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i)));
}
std::vector<uint8_t> Input(const char* id, uint32_t rows, int cols) {
    std::vector<uint8_t> b = {'R','S','7','X',1,0,16,0};
    U32(b, 0); U32(b, 2);
    b.insert(b.end(), {'S','T','R','S',1,0,1,0});
    U32(b, 4 + cols);
    b.insert(b.end(), {0, uint8_t(6 + cols), uint8_t(cols), 0});
    for (int i = 0; i < cols; ++i) b.push_back(uint8_t(6 + i));
    b.insert(b.end(), id, id + 4);
    b.insert(b.end(), {1,0,1,0}); U32(b, 4); U32(b, rows);
    uint32_t size = uint32_t(b.size()); std::memcpy(b.data() + 8, &size, 4);
    return b;
}
int main() {
    for (int cols : {1,2,10}) for (uint32_t rows : {0x10000000u, 0x40000000u, 0x80000000u, 0xffffffffu}) {
        for (const char* id : {"CHDT", "CHNT"}) {
            auto bytes = Input(id, rows, cols);
            rs7ext::Block b; char why[160];
            try {
                if (rs7ext::Parse(bytes.data(), bytes.size(), rows, rows, 6 + cols, &b, why, sizeof why) != rs7ext::Status::Invalid) {
                    std::printf("FAIL: %s cols %d rows %u accepted\n", id, cols, rows); return 1;
                }
            } catch (...) { std::printf("FAIL: %s cols %d rows %u allocated before validation\n", id, cols, rows); return 2; }
        }
    }
    rs7ext::Block b;
    b.strings = {6,7}; b.hasTemplates = true; b.fret = {2,3}; b.finger = {1,2};
    int f, g;
    if (b.TemplateColumn(0x80000000u, 6, &f, &g)) { std::puts("FAIL: template index wrapped"); return 3; }
    if (oversized) return 4;
    std::puts("PASS: malformed count matrix rejected before allocation; template lookup does not wrap");
}
