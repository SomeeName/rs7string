#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
static int failArray = -1;
static bool failHeap = false;

void* operator new[](size_t n) {
    if (failArray == 0) {
        failArray = -1;
        throw std::bad_alloc();
    }

    if (failArray > 0) {
        --failArray;
    }
    if (void* p = std::malloc(n ? n : 1)) {
        return p;
    }

    throw std::bad_alloc();
}

void operator delete[](void* p) noexcept {
    std::free(p);
}

void* TestHeapAlloc(HANDLE heap, DWORD flags, SIZE_T n) {
    if (failHeap) {
        failHeap = false;
        return nullptr;
    }

    return HeapAlloc(heap, flags, n);
}

#define HeapAlloc TestHeapAlloc
#include "features/diagrams/hooks_mid.cpp"
#include "features/nslot/hooks.cpp"
#include "features/nslot/records.cpp"
#include "platform/safecopy.cpp"  // Rs7_SafeCopy* (the shared safe-copy helpers)
#undef HeapAlloc
extern "C" uintptr_t g_rs7_diagrams = 0, g_rs7_publish_ports = 0;
static int arrays = 0;

void Rs7Log(const char*, ...) {}

void Rs7Alloc_Inc(Rs7AllocKind) {
    ++arrays;
}

void Rs7Alloc_Dec(Rs7AllocKind) {
    --arrays;
}

void Rs7Diagram_Drop(void*) {}

extern "C" void __cdecl Rs7Diagram_Bind(void*, void*) {}

extern "C" void __cdecl Rs7Diagram_Manifest(void*, void*, void*, void*, void*, int) {}

extern "C" void __cdecl Rs7Diagram_Loaded(void**, const uint8_t*) {}

extern "C" void __cdecl Rs7Mid_Fldcw(uint32_t) {}

std::shared_ptr<const rs7ext::Block> Rs7Ext_Find(const void*) {
    return {};
}

int rs7ext::Block::Column(int) const {
    return -1;
}

bool rs7ext::Block::TemplateColumn(uint32_t, int, int*, int*) const {
    return false;
}

void Vector(uint8_t* song, size_t off, uintptr_t begin, uintptr_t end) {
    uintptr_t values[2] = {begin, end};

    std::memcpy(song + off, values, sizeof values);
}

// TP13's stock serializer can keep a stock pointer: DGS checks membership before widening.
bool DiagramConsumer(uintptr_t begin, uint32_t id, uintptr_t record) {
    uint8_t holder[0x120] = {};

    Vector(holder, 0x94, begin, begin);

    for (uint32_t on = 0; on < 2; ++on) {
        Rs7Mid::Context c = {1, reinterpret_cast<uint32_t>(holder), 3, 4, 5, 6, 7, id, 9, 0, 11};
        Rs7Mid::Context want = c;

        want.edx = static_cast<uint32_t>(begin);
        want.ecx = id * 9;
        want.eax = on ? static_cast<uint32_t>(record) : static_cast<uint32_t>(begin) + id * rs7ns::kTplSize;
        want.jump = static_cast<uint32_t>(g_rs7_resume_TP13);
        g_rs7_diagrams = on;
        Rs7_Mid_TP13(c);
        g_rs7_diagrams = 0;

        if (std::memcmp(&c, &want, sizeof c)) {
            std::puts("FAIL: TP13 pointer or preserved registers");
            return false;
        }
    }

    return true;
}

// Catches stock-sized pointers escaping failed construction into the widened render / matcher readers.
bool EmptyConsumers(uintptr_t begin, uintptr_t cnBegin, uint32_t id = 0) {
    using namespace rs7ns;
    const auto* t8 = reinterpret_cast<const uint8_t*>(Rs7Ns_TplRecord(static_cast<uint32_t>(begin), id));
    const auto* c8 = reinterpret_cast<const uint8_t*>(Rs7Ns_CnRecord(static_cast<uint32_t>(cnBegin), id));

    if (reinterpret_cast<uintptr_t>(t8) == begin || reinterpret_cast<uintptr_t>(c8) == cnBegin) {
        std::puts("FAIL: stock-sized record reached widened consumer");
        return false;
    }

    if (Rs7Ns_AsT8(t8) != t8 || Rs7Ns_AsC8(c8) != c8) {
        return false;
    }
    if (!DiagramConsumer(begin, id, static_cast<uint32_t>(begin) + id * kTplSize)) {
        return false;
    }

    for (uint32_t s = 0; s < 8; ++s) {
        uint32_t bits = 0xFFFFFFFF;
        if (!StringMask8(c8, s, 0xFFFFFFFF, &bits) || bits) {
            return false;
        }
        if (t8[kT8Frets + s] != 0xFF || t8[kT8Fingers + s] != 0xFF ||
            *reinterpret_cast<const int32_t*>(t8 + kT8Midi + 4 * s) != -1) {
            return false;
        }
        if (c8[kC8SlideTo + s] != 0xFF || c8[kC8SlideUnp + s] != 0xFF ||
            *reinterpret_cast<const int16_t*>(c8 + kC8Vib + 2 * s)) {
            return false;
        }

        for (uint32_t b = 0; b < kBendSize; ++b) {
            if (c8[kC8Bends + s * kBendSize + b]) {
                return false;
            }
        }
    }

    const FretSpan span =
        DoubleStopSpan(static_cast<uint32_t>(begin), id, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(t8)));
    if (span.frets != reinterpret_cast<uintptr_t>(t8 + kT8Frets) || span.count != 8) {
        return false;
    }
    if (t8[kT8Slots] != 6 || t8[kT8Used]) {
        return false;
    }

    uint8_t eval[0xC28];

    std::memset(eval, 0x5A, sizeof eval);
    Rs7Ns_OnEvalBuilt(eval, t8, c8);

    return *reinterpret_cast<const int32_t*>(eval + kEvMidi67) == -1 &&
           *reinterpret_cast<const int32_t*>(eval + kEvMidi67 + 4) == -1 &&
           *reinterpret_cast<const uint32_t*>(eval + kEvMask67) == 0;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        return 9;
    }

    const char* mode = argv[1];
    uint8_t song[0x120] = {}, tpl[rs7ns::kTplSize] = {}, cn[rs7ns::kCnSize] = {};
    auto restore = [&]() {
        Vector(song, 0x94, reinterpret_cast<uintptr_t>(tpl), reinterpret_cast<uintptr_t>(tpl + sizeof tpl));
        Vector(song, 0xAC, reinterpret_cast<uintptr_t>(cn), reinterpret_cast<uintptr_t>(cn + sizeof cn));
    };

    restore();
    Rs7Ns_OnSngLoaded(song);
    Rs7Ns_OnPostProcessed(song);
    if (!g_rs7_ns[0].tplBegin || !g_rs7_ns[0].cnBegin || arrays != 2) {
        return 8;
    }
    if (!DiagramConsumer(reinterpret_cast<uintptr_t>(tpl), 0, g_rs7_ns[0].t8) ||
        !DiagramConsumer(reinterpret_cast<uintptr_t>(tpl), 1, reinterpret_cast<uintptr_t>(tpl) + rs7ns::kTplSize)) {
        return 6;
    }

    bool reload = !std::strcmp(mode, "reload");

    if (reload || !std::strcmp(mode, "partial")) {
        Vector(song, 0x94, reinterpret_cast<uintptr_t>(tpl), reinterpret_cast<uintptr_t>(tpl + sizeof tpl - 1));
    } else if (!std::strcmp(mode, "template-copy")) {
        Vector(song, 0x94, 1, 1 + rs7ns::kTplSize);
    } else if (!std::strcmp(mode, "record-copy")) {
        Vector(song, 0xAC, 1, 1 + rs7ns::kCnSize);
    } else if (!std::strcmp(mode, "template-allocation")) {
        failArray = 0;
    } else if (!std::strcmp(mode, "scratch-allocation")) {
        failArray = 1;
    } else if (!std::strcmp(mode, "c8-allocation")) {
        failHeap = true;
    } else if (!std::strcmp(mode, "t8-allocation")) {
        Rs7Ns_OnSongFree(song);
        failHeap = true;
        reload = true;
    } else if (!std::strcmp(mode, "load-scratch-allocation")) {
        Rs7Ns_OnSongFree(song);
        failArray = 0;
        reload = true;
    } else if (!std::strcmp(mode, "user-allocation")) {
        g_rs7_ns[0].origCn = 0;
        failArray = 2;
    } else if (!std::strcmp(mode, "registry-full")) {
        Rs7Ns_OnSongFree(song);

        for (int i = 0; i < kRs7NsEntries; ++i) {
            g_rs7_ns[i].song = i + 1;
        }

        reload = true;
    } else {
        return 7;
    }

    if (reload) {
        Rs7Ns_OnSngLoaded(song);
    } else {
        Rs7Ns_OnPostProcessed(song);
    }
    const Rs7NsEntry& e = g_rs7_ns[0];

    if (e.tplBegin || e.cnBegin || e.t8 || e.c8 || e.tplCount || e.cnCount || arrays) {
        std::printf("FAIL: %s retained published or allocated arrays\n", mode);
        return 1;
    }

    const bool full = !std::strcmp(mode, "registry-full");
    const bool rejectedLoad = !std::strcmp(mode, "reload");
    if (!full && e.song != (rejectedLoad ? 0 : reinterpret_cast<uintptr_t>(song))) {
        return 2;
    }
    if (!reload && e.origCn != (!std::strcmp(mode, "user-allocation") ? 0u : 1u)) {
        return 2;
    }

    failArray = 0;
    failHeap = true;
    if (!EmptyConsumers(reinterpret_cast<uintptr_t>(tpl), reinterpret_cast<uintptr_t>(cn))) {
        return 6;
    }
    // A lookup of an invalid id / unreadable vector must be safe too, without touching the stock bytes.
    if (!EmptyConsumers(1, 1, 0xFFFFFFFF) || !EmptyConsumers(0, 0)) {
        return 6;
    }
    if (failArray != 0 || !failHeap) {
        return 6;
    }

    failArray = -1;
    failHeap = false;

    if (!TryAcquireSRWLockExclusive(&g_lock)) {
        std::puts("FAIL: writer lock held");
        return 3;
    }

    ReleaseSRWLockExclusive(&g_lock);
    if (full) {
        std::memset(g_rs7_ns, 0, sizeof g_rs7_ns);
    }
    restore();
    if (reload) {
        Rs7Ns_OnSngLoaded(song);
    }
    Rs7Ns_OnPostProcessed(song);

    if (!e.tplBegin || !e.cnBegin || arrays != 2) {
        std::puts("FAIL: retry failed");
        return 4;
    }

    Rs7Ns_OnSongFree(song);
    if (arrays) {
        return 5;
    }

    std::printf("PASS: %s uses safe empty widened consumers, releases lock, and permits retry\n", mode);
}
