#include "features/diagrams/diagrams.h"
#include "features/nslot/nslot.h"
#include "test.h"
#include "testutil.h"
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern "C" uintptr_t g_rsxr_dkey, g_rsxr_dnumber, g_rsxr_dset, g_rsxr_ddrop;

namespace {
struct Json {
    uintptr_t* vt;
    int refs = 1;
    double number = -1;
    Json* dictionary = nullptr;
};

uintptr_t vt[24] = {};
Json id, emitted;
std::map<std::string, int> props;

void __fastcall Add(Json* j, void*) {
    ++j->refs;
}

void __fastcall Release(Json* j, void*) {
    --j->refs;
}

void* __fastcall Number(Json* j, void*) {
    return j;
}

void* __fastcall Dictionary(Json* j, void*) {
    return j->dictionary;
}

void* __fastcall Find(Json*, void*, const char* key) {
    return !std::strcmp(key, "ChordId") ? &id : nullptr;
}

void __fastcall Key(const char* text, void*, void** out) {
    *out = const_cast<char*>(text);
}

void __stdcall Set(void*, void** key, void** num) {
    props[static_cast<char*>(*key)] = int(static_cast<Json*>(*num)->number);
}

void __cdecl MakeNumber(void** out, const double* d) {
    emitted = {vt, 1, *d, nullptr};
    *out = &emitted;
}

__declspec(naked) void NumberBridge() {
    __asm lea eax, [esp + 4] __asm push eax __asm push edi __asm call MakeNumber __asm add esp, 8 __asm ret
}

__declspec(naked) void DropKey() {
    __asm ret
}

struct Array {
    uintptr_t pad[4] = {};
    Json** begin;
    Json** end;
};

// Authored extension block: seven strings, CHDT IDs 0/1, string6 fret2/finger1 then mute.
const uint8_t block[] = {'R', 'S', 'X', 'R', 1, 0, 16, 0, 0x4e, 0, 0,    0,   3,   0,   0,   0,   'S', 'T', 'R', 'S',
                         1,   0,   1,   0,   5, 0, 0,  0, 0,    7, 1,    0,   6,   'C', 'H', 'D', 'T', 1,   0,   1,
                         0,   8,   0,   0,   0, 2, 0,  0, 0,    2, 1,    255, 255, 'C', 'H', 'N', 'T', 1,   0,   0,
                         0,   13,  0,   0,   0, 1, 0,  0, 0,    0, 0x20, 0,   0,   255, 255, 0,   0,   0};

void VectorAt(uint8_t* song, int offset, void* begin, size_t len) {
    uintptr_t v[] = {reinterpret_cast<uintptr_t>(begin), reinterpret_cast<uintptr_t>(begin) + len, 0};

    std::memcpy(song + offset, v, sizeof(v));
}
}  // namespace

TEST(diagram_manifest_extblock_binding_is_identity_scoped_and_releases_dictionary) {
    vt[0] = reinterpret_cast<uintptr_t>(&Add);
    vt[1] = reinterpret_cast<uintptr_t>(&Release);
    vt[0x18 / 4] = reinterpret_cast<uintptr_t>(&Find);
    vt[0x54 / 4] = reinterpret_cast<uintptr_t>(&Number);
    vt[0x5c / 4] = reinterpret_cast<uintptr_t>(&Dictionary);
    Json dictA{vt}, dictB{vt}, attrsA{vt}, attrsB{vt}, chord{vt};

    attrsA.dictionary = &dictA;
    attrsB.dictionary = &dictB;
    id = {vt, 1, 0};
    Json nodes[6];
    Json* pointers[6];

    for (int i = 0; i < 6; ++i) {
        nodes[i] = {vt, 1, -1};
        pointers[i] = &nodes[i];
    }

    Array array{{}, pointers, pointers + 6};
    uint8_t song[0x130] = {}, otherSong[0x130] = {}, stream[0x20] = {};
    uint8_t stock[rsxrns::kTplSize * 2] = {};

    std::memset(stock + 4, 255, 12);
    std::memset(stock + rsxrns::kTplSize + 4, 255, 12);
    int16_t tuning[7] = {};
    static uint8_t cn[rsxrns::kCnSize] = {};

    VectorAt(song, 0x94, stock, sizeof(stock));
    VectorAt(song, 0xac, cn, sizeof(cn));
    VectorAt(song, 0x110, tuning, sizeof(tuning));
    uintptr_t tail = reinterpret_cast<uintptr_t>(block);
    uint32_t size = sizeof(block);

    std::memcpy(stream + 0x10, &tail, 4);
    std::memcpy(stream + 0x18, &size, 4);
    RsxrExt_OnSngLoaded(song, stream);
    CHECK(RsxrExt_Find(song) != nullptr);
    uint8_t t8[rsxrns::kT8Size * 2];
    const rsxrns::Col ext[2] = {{2, 1}, {-1, -1}}, none[2];

    rsxrns::BuildT8(stock, ext, tuning, 7, 255, t8);
    rsxrns::BuildT8(stock + rsxrns::kTplSize, none, tuning, 7, 255, t8 + rsxrns::kT8Size);
    rsxrtest::Restore restore_g_rsxr_ns_63_(g_rsxr_ns[63]);

    g_rsxr_ns[63] = {reinterpret_cast<uintptr_t>(stock), 2, reinterpret_cast<uintptr_t>(t8), 0, 0, 0,
                     reinterpret_cast<uintptr_t>(song),  0};
    uintptr_t saved[] = {g_rsxr_dkey, g_rsxr_dnumber, g_rsxr_dset, g_rsxr_ddrop, g_rsxr_diagrams};

    g_rsxr_dkey = reinterpret_cast<uintptr_t>(&Key);
    g_rsxr_dnumber = reinterpret_cast<uintptr_t>(&NumberBridge);
    g_rsxr_dset = reinterpret_cast<uintptr_t>(&Set);
    g_rsxr_ddrop = reinterpret_cast<uintptr_t>(&DropKey);
    g_rsxr_diagrams = 1;
    RsxrDiagram_Bind(song, &attrsA);
    RsxrDiagram_Bind(otherSong, &attrsB);
    CHECK_EQ(dictA.refs, 2);
    CHECK_EQ(dictB.refs, 2);
    props.clear();
    RsxrDiagram_Manifest(&props, &array, &array, &attrsA, &chord, 0);
    CHECK_EQ(props["LowBFret"], 2);
    CHECK_EQ(props["LowB_Finger"], 1);
    CHECK_EQ(emitted.refs, 0);
    props.clear();
    RsxrDiagram_Manifest(&props, &array, &array, &attrsB, &chord, 0);
    CHECK(props.empty());
    // A matching six-column shape in a different arrangement never borrows A's extension.
    id.number = 1;
    props.clear();
    RsxrDiagram_Manifest(&props, &array, &array, &attrsA, &chord, 0);
    CHECK_EQ(props["LowBFret"], -1);
    CHECK(props.count("LowB_Finger") == 0);
    id.number = 2;
    props.clear();
    RsxrDiagram_Manifest(&props, &array, &array, &attrsA, &chord, 0);
    CHECK(props.empty());
    RsxrDiagram_Drop(song);
    RsxrDiagram_Drop(otherSong);
    CHECK_EQ(dictA.refs, 1);
    CHECK_EQ(dictB.refs, 1);
    id.number = 0;
    props.clear();
    RsxrDiagram_Manifest(&props, &array, &array, &attrsA, &chord, 0);
    CHECK(props.empty());
    g_rsxr_dkey = saved[0];
    g_rsxr_dnumber = saved[1];
    g_rsxr_dset = saved[2];
    g_rsxr_ddrop = saved[3];
    g_rsxr_diagrams = saved[4];
    // Clear the synthetic block so recycled stack addresses cannot keep it.
    uint8_t empty[0x20] = {};

    RsxrExt_OnSngLoaded(song, empty);
}
