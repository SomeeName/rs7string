#include "d3d/texlist.h"
#include "test.h"
#include <cstring>
#include <map>
#include <string>

namespace {
constexpr uintptr_t kD3d9Lo = 0x72B00000, kD3d9Hi = 0x72C7A000, kD3dVt = 0x72B01438;
constexpr uintptr_t kHead = 0x00100000;

// Sparse fake address space: a read fails if any byte is missing.
struct FakeMem : RsxrMemReader {
    std::map<uintptr_t, uint8_t> b;

    bool Read(uintptr_t a, void* out, size_t n) const override {
        auto* o = static_cast<uint8_t*>(out);

        for (size_t i = 0; i < n; ++i) {
            const auto it = b.find(a + i);
            if (it == b.end()) {
                return false;
            }

            o[i] = it->second;
        }

        return true;
    }

    void U32(uintptr_t a, uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            b[a + i] = uint8_t(v >> (8 * i));
        }
    }

    void Str(uintptr_t a, const char* s) {
        for (size_t i = 0; i <= std::strlen(s); ++i) {
            b[a + i] = uint8_t(s[i]);
        }
    }
};

struct NodeSpec {
    uint32_t vt = rsxrtex::kVtSource50;
    const char* file = nullptr;
    const char* plat = nullptr;
    bool rd = true;
    uint32_t rdVt = rsxrtex::kVtRendData;
    uint32_t w = 256, h = 128, fmt = 22;
    bool d3d = true;
    uint32_t d3dVt = uint32_t(kD3dVt);
};

// Node i lives at 0x1000*(i+1): object (links at +0x30/+0x34), renderer data at +0x400, d3d at +0x600,
// names at +0x800/+0xA00.
uintptr_t Obj(size_t i) {
    return 0x1000 * (i + 1);
}

FakeMem Build(const std::vector<NodeSpec>& nodes) {
    FakeMem m;

    m.U32(kHead, nodes.empty() ? 0 : uint32_t(Obj(0)));

    for (size_t i = 0; i < nodes.size(); ++i) {
        const NodeSpec& n = nodes[i];
        const uintptr_t o = Obj(i), rd = o + 0x400, d3d = o + 0x600;

        for (uintptr_t a = o; a < o + 0x60; a += 4) {
            m.U32(a, 0);  // the whole object is readable
        }

        m.U32(o, n.vt);
        m.U32(o + 0x30, i ? uint32_t(Obj(i - 1)) : 0);
        m.U32(o + rsxrtex::kNext, i + 1 < nodes.size() ? uint32_t(Obj(i + 1)) : 0);

        if (n.file) {
            m.Str(o + 0x800, n.file);
            m.U32(o + rsxrtex::kFile, uint32_t(o + 0x800));
        }

        if (n.plat) {
            m.Str(o + 0xA00, n.plat);
            m.U32(o + rsxrtex::kPlatFile, uint32_t(o + 0xA00));
        }

        if (!n.rd) {
            continue;
        }

        m.U32(o + rsxrtex::kRendData, uint32_t(rd));

        for (uintptr_t a = rd; a < rd + 0x68; a += 4) {
            m.U32(a, 0);
        }

        m.U32(rd, n.rdVt);
        m.U32(rd + rsxrtex::kRdOwner, uint32_t(o));
        m.U32(rd + rsxrtex::kRdW, n.w);
        m.U32(rd + rsxrtex::kRdH, n.h);
        m.U32(rd + rsxrtex::kRdFmt, n.fmt);
        if (!n.d3d) {
            continue;
        }

        m.U32(rd + rsxrtex::kRdD3d, uint32_t(d3d));
        m.U32(d3d, n.d3dVt);
    }

    return m;
}

bool Contains(const std::string& s, const char* part) {
    return s.find(part) != std::string::npos;
}
}  // namespace

TEST(texlist_unnamed_256x128_nif_textures_are_embedded_candidates) {
    std::vector<NodeSpec> n(5);

    n[1].w = 512;                    // another size
    n[2].vt = rsxrtex::kVtSource54;  // a file texture without a name
    n[3].file = "data/x/other.dds";  // named, not a ramp
    n[4].plat = "";                  // empty platform name: still unnamed
    FakeMem m = Build(n);
    const RsxrTexWalk w = RsxrTex_Walk(m, kHead, kD3d9Lo, kD3d9Hi);

    CHECK_EQ(w.nameMatches, 0u);
    CHECK_EQ(w.found.size(), size_t(2));
    CHECK(w.found[0].obj == Obj(0) && w.found[0].family == RsxrFamily::Embedded);
    CHECK(w.found[1].obj == Obj(4) && w.found[1].family == RsxrFamily::Embedded);
    CHECK(Contains(w.found[0].path, "NIF texture"));
    CHECK(w.rejects.empty());
    CHECK(RsxrTex_Revalidate(m, Obj(0), Obj(0) + 0x600, RsxrFamily::Embedded, kD3d9Lo, kD3d9Hi));
    CHECK(!RsxrTex_Revalidate(m, Obj(3), Obj(3) + 0x600, RsxrFamily::Embedded, kD3d9Lo, kD3d9Hi));
    CHECK(!RsxrTex_Revalidate(m, Obj(0), Obj(0) + 0x600, RsxrFamily::Note, kD3d9Lo, kD3d9Hi));
}

TEST(texlist_finds_both_families_in_a_fake_list) {
    std::vector<NodeSpec> n(6);

    n[0].file = "note_gradient_map.dds";
    n[0].plat = "data/assets/gameplay/note/note_gradient_map.dds";
    n[1].vt = rsxrtex::kVtSource54;
    n[1].file = "gradient_map_additive.dds";
    n[1].plat = "data/assets/gameplay/stem/gradient_map_additive.dds";
    n[2].file = "data/assets/ui/unrelated.dds";
    n[3].file = "note_gradient_map.dds";  // bare name, no platform name
    n[4].file = "data\\assets\\gameplay\\tail\\note_gradient_map.dds";
    n[4].plat = "data\\assets\\gameplay\\tail\\note_gradient_map.dds";
    n[5].vt = 0x011E9530;  // not a source texture
    n[5].file = "note_gradient_map.dds";
    const FakeMem m = Build(n);
    const RsxrTexWalk w = RsxrTex_Walk(m, kHead, kD3d9Lo, kD3d9Hi);

    CHECK_EQ(w.nodes, 6u);
    CHECK_EQ(w.sources, 5u);
    CHECK_EQ(w.nameMatches, 4u);
    CHECK(!w.cycleOrCap && !w.readFault);
    CHECK(w.rejects.empty());
    CHECK_EQ(w.found.size(), size_t(4));

    if (w.found.size() == 4) {
        CHECK(w.found[0].family == RsxrFamily::Note && w.found[0].obj == Obj(0));
        CHECK_EQ(std::string(w.found[0].path), std::string("data/assets/gameplay/note/note_gradient_map.dds"));
        CHECK(w.found[1].family == RsxrFamily::Additive && w.found[1].obj == Obj(1));
        CHECK(w.found[2].family == RsxrFamily::Note && w.found[2].obj == Obj(3));
        CHECK_EQ(std::string(w.found[2].path), std::string("note_gradient_map.dds"));  // no platform name
        CHECK(w.found[3].family == RsxrFamily::Note && w.found[3].obj == Obj(4));
        CHECK_EQ(w.found[0].rd, Obj(0) + 0x400);
        CHECK_EQ(w.found[0].d3d, Obj(0) + 0x600);
        CHECK_EQ(w.found[0].fmt, 22u);
    }

    CHECK_EQ(w.anyD3d, Obj(0) + 0x600);
}

TEST(texlist_any_d3d_comes_from_an_unrelated_texture_too) {
    std::vector<NodeSpec> n(2);

    n[0].file = "data/assets/ui/unrelated.dds";
    n[0].w = 64;  // any size will do for device acquisition
    n[1].file = "note_gradient_map.dds";
    n[1].d3dVt = 0x10000000;  // rejected
    const RsxrTexWalk w = RsxrTex_Walk(Build(n), kHead, kD3d9Lo, kD3d9Hi);

    CHECK_EQ(w.anyD3d, Obj(0) + 0x600);
    CHECK(w.found.empty());
}

TEST(texlist_rejects_and_says_why) {
    std::vector<NodeSpec> n(4);

    for (auto& s : n) {
        s.file = "note_gradient_map.dds";
    }

    n[0].rdVt = 0x01234567;
    n[1].h = 256;
    n[2].d3dVt = 0x10000000;
    n[3].rd = false;
    const RsxrTexWalk w = RsxrTex_Walk(Build(n), kHead, kD3d9Lo, kD3d9Hi);

    CHECK(w.found.empty());
    CHECK_EQ(w.nameMatches, 4u);
    CHECK_EQ(w.anyD3d, uintptr_t(0) + Obj(1) + 0x600);  // the 256x256 one is still a valid d3d texture
    CHECK_EQ(w.rejects.size(), size_t(4));

    if (w.rejects.size() == 4) {
        CHECK(Contains(w.rejects[0], "rd vtable") && Contains(w.rejects[0], "01234567"));
        CHECK(Contains(w.rejects[1], "size") && Contains(w.rejects[1], "256x256"));
        CHECK(Contains(w.rejects[2], "d3d vtable") && Contains(w.rejects[2], "10000000"));
        CHECK(Contains(w.rejects[3], "no renderer data"));
        CHECK(Contains(w.rejects[0], "obj=00001000 'note_gradient_map.dds'"));
    }
}

TEST(texlist_stops_on_a_cycle_and_on_unreadable_memory) {
    std::vector<NodeSpec> n(2);

    n[0].file = n[1].file = "note_gradient_map.dds";
    FakeMem cyc = Build(n);

    cyc.U32(Obj(1) + rsxrtex::kNext, uint32_t(Obj(0)));  // 1 -> 0
    const RsxrTexWalk a = RsxrTex_Walk(cyc, kHead, kD3d9Lo, kD3d9Hi);

    CHECK(a.cycleOrCap && !a.readFault);
    CHECK_EQ(a.nodes, 2u);
    CHECK_EQ(a.found.size(), size_t(2));  // found before the cycle stays valid

    FakeMem bad = Build(n);

    bad.U32(Obj(1) + rsxrtex::kNext, 0x00DEAD00);  // unmapped
    const RsxrTexWalk b = RsxrTex_Walk(bad, kHead, kD3d9Lo, kD3d9Hi);

    CHECK(b.readFault && !b.cycleOrCap);
    CHECK_EQ(b.nodes, 2u);

    FakeMem nohead;
    const RsxrTexWalk c = RsxrTex_Walk(nohead, kHead, kD3d9Lo, kD3d9Hi);

    CHECK(c.readFault && c.nodes == 0);

    FakeMem empty = Build({});
    const RsxrTexWalk d = RsxrTex_Walk(empty, kHead, kD3d9Lo, kD3d9Hi);

    CHECK(!d.readFault && d.nodes == 0 && d.found.empty());

    FakeMem badName = Build(n);  // a name pointer into nothing: no match, no fault

    badName.U32(Obj(0) + rsxrtex::kFile, 0x00BAD000);
    const RsxrTexWalk e = RsxrTex_Walk(badName, kHead, kD3d9Lo, kD3d9Hi);

    // Node 0 now has no readable name: a 256x128 NIF texture, an embedded candidate.
    CHECK(!e.readFault && e.nameMatches == 1 && e.found.size() == 2);
    CHECK(e.found[0].family == RsxrFamily::Embedded);

    FakeMem longName = Build(n);  // no NUL within 120 bytes: no match

    longName.Str(Obj(0) + 0x800, (std::string(130, 'a') + "/note_gradient_map.dds").c_str());
    const RsxrTexWalk f = RsxrTex_Walk(longName, kHead, kD3d9Lo, kD3d9Hi);

    CHECK_EQ(f.nameMatches, 1u);
}

TEST(texlist_family_by_basename) {
    RsxrFamily f = RsxrFamily::Additive;

    CHECK(RsxrTex_FamilyOf("data/assets/gameplay/NOTE/Note_Gradient_Map.DDS", &f) && f == RsxrFamily::Note);
    CHECK(!RsxrTex_FamilyOf("x_note_gradient_map.dds", &f));
    CHECK(RsxrTex_FamilyOf("gradient_map_additive.dds", &f) && f == RsxrFamily::Additive);
    CHECK(RsxrTex_FamilyOf("data\\assets\\gameplay\\tail\\note_gradient_map.dds", &f) && f == RsxrFamily::Note);
    CHECK(!RsxrTex_FamilyOf("note_gradient_map.dds.bak", &f));
    CHECK(!RsxrTex_FamilyOf("", &f));
    CHECK(!RsxrTex_FamilyOf(nullptr, &f));
}

// Batch B review: before the render thread takes a reference on a candidate, it re-checks under the
// list lock that the pointer is still the d3d texture of a live ramp source texture.
TEST(texlist_revalidate_accepts_a_live_ramp_and_rejects_reuse) {
    std::vector<NodeSpec> n(2);

    n[0].file = "data/assets/gameplay/note/note_gradient_map.dds";
    n[1].file = "gradient_map_additive.dds";
    FakeMem m = Build(n);
    const uintptr_t d3d0 = Obj(0) + 0x600;

    CHECK(RsxrTex_Revalidate(m, Obj(0), d3d0, RsxrFamily::Note, kD3d9Lo, kD3d9Hi));
    CHECK(RsxrTex_Revalidate(m, Obj(1), Obj(1) + 0x600, RsxrFamily::Additive, kD3d9Lo, kD3d9Hi));
    CHECK(!RsxrTex_Revalidate(m, Obj(0), d3d0, RsxrFamily::Additive, kD3d9Lo, kD3d9Hi));        // other family
    CHECK(!RsxrTex_Revalidate(m, Obj(0), Obj(1) + 0x600, RsxrFamily::Note, kD3d9Lo, kD3d9Hi));  // other d3d
    CHECK(!RsxrTex_Revalidate(m, 0, d3d0, RsxrFamily::Note, kD3d9Lo, kD3d9Hi));
    FakeMem moved = Build(n);  // renderer data now holds another texture

    moved.U32(Obj(0) + 0x400 + rsxrtex::kRdD3d, uint32_t(Obj(1) + 0x600));
    CHECK(!RsxrTex_Revalidate(moved, Obj(0), d3d0, RsxrFamily::Note, kD3d9Lo, kD3d9Hi));
    FakeMem freed = Build(n);  // object destroyed: vtable changed

    freed.U32(Obj(0), 0x011E9530);
    CHECK(!RsxrTex_Revalidate(freed, Obj(0), d3d0, RsxrFamily::Note, kD3d9Lo, kD3d9Hi));
    FakeMem renamed = Build(n);  // memory reused by another texture

    renamed.Str(Obj(0) + 0x800, "data/assets/ui/other.dds");
    CHECK(!RsxrTex_Revalidate(renamed, Obj(0), d3d0, RsxrFamily::Note, kD3d9Lo, kD3d9Hi));
    FakeMem gone;  // unreadable

    CHECK(!RsxrTex_Revalidate(gone, Obj(0), d3d0, RsxrFamily::Note, kD3d9Lo, kD3d9Hi));
}

TEST(texlist_walk_into_reuses_preallocated_storage) {
    std::vector<NodeSpec> n(3);

    n[0].file = "note_gradient_map.dds";
    n[1].file = "gradient_map_additive.dds";
    n[2].file = "note_gradient_map.dds";
    n[2].h = 256;  // one reject
    const FakeMem m = Build(n);
    RsxrTexWalk w;

    RsxrTex_Prepare(&w);
    const size_t capFound = w.found.capacity(), capRej = w.rejects.capacity();

    CHECK(capFound >= 64 && capRej >= 64);

    for (int round = 0; round < 3; ++round) {
        RsxrTex_WalkInto(m, kHead, kD3d9Lo, kD3d9Hi, &w);
        CHECK_EQ(w.nodes, 3u);
        CHECK_EQ(w.found.size(), size_t(2));
        CHECK_EQ(w.rejects.size(), size_t(1));
        CHECK(!w.cycleOrCap && !w.readFault);
        CHECK(w.found.capacity() == capFound && w.rejects.capacity() == capRej);
    }

    FakeMem cyc = Build(n);  // exact cycle detection without a set

    cyc.U32(Obj(2) + rsxrtex::kNext, uint32_t(Obj(1)));
    RsxrTex_WalkInto(cyc, kHead, kD3d9Lo, kD3d9Hi, &w);
    CHECK(w.cycleOrCap && w.nodes == 3u);
}
