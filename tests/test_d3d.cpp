// clang-format off: initguid.h must come before d3d9.h (sorting would put it after)
#include <windows.h>
#include <initguid.h>   // this file only: defines the SDK's IIDs for iid_texture9_matches_the_sdk
#include <d3d9.h>
// clang-format on
#include "core/dxt.h"
#include "core/ramp.h"
#include "core/rampstate.h"
#include "d3d/iids.h"
#include "d3d/swap.h"
#include "d3d/worker.h"
#include "d3d_fixture.h"
#include "hooks/registry.h"
#include "log.h"
#include "ramp_fixture.h"
#include "test.h"
#include <cstring>
#include <string>
#include <vector>

namespace {
using rs7d3d::Device;
using rs7fix::Stock;
const Rs7SwapCfg kCfg = {{0xC8C8C8, 0x00FFFF}, false};

std::vector<std::string>& Lines() {
    static std::vector<std::string> v;

    return v;
}

void Capture(const char* msg) {
    Lines().push_back(msg);
}

struct Capturing {
    Capturing() {
        Lines().clear();
        Rs7Log_SetSink(Capture);
    }

    ~Capturing() { Rs7Log_SetSink(nullptr); }
};

bool Logged(const char* a, const char* b = "") {
    for (const auto& l : Lines()) {
        if (l.find(a) != std::string::npos && l.find(b) != std::string::npos) {
            return true;
        }
    }

    return false;
}

std::string Stats() {
    Capturing cap;

    Rs7Ramp_LogStats();

    return Lines().empty() ? std::string() : Lines()[0];
}

bool Has(const std::string& s, const char* part) {
    return s.find(part) != std::string::npos;
}

bool V6On() {
    return g_rs7_v6[0] == 0.4453125f && g_rs7_v6[1] == 0.9453125f;
}

bool V6Off() {
    return g_rs7_v6[0] == 0.0f && g_rs7_v6[1] == 0.0f;
}

// The engine revalidation needs the game; tests decide the outcome.
int g_verdict = 1;

int Validate(const Rs7Cand& c, IDirect3DTexture9** out) {
    if (g_verdict != 1) {
        return g_verdict;
    }

    return SUCCEEDED(static_cast<IDirect3DBaseTexture9*>(c.d3d)->QueryInterface(IID_IDirect3DTexture9,
                                                                                reinterpret_cast<void**>(out)))
               ? 1
               : 2;
}

void Begin(const Rs7SwapCfg& cfg = kCfg) {
    Rs7Swap_UninstallForTests();
    Rs7Cand_ResetForTests();
    Rs7Swap_Configure(cfg);
    g_verdict = 1;
    Rs7Swap_SetValidatorForTests(Validate);
}

void End(IDirect3DDevice9* dev) {
    for (DWORD s = 0; s < 4; ++s) {
        dev->SetTexture(s, nullptr);
    }

    Rs7Swap_UninstallForTests();
    Rs7Cand_ResetForTests();
    Rs7Swap_SetValidatorForTests(nullptr);
}

IDirect3DBaseTexture9* RawBound(IDirect3DDevice9* dev, DWORD stage) {
    IDirect3DBaseTexture9* t = nullptr;

    Rs7Swap_RawGetTextureForTests(dev, stage, &t);
    if (t) {
        t->Release();  // the test still holds its own references; compare only
    }

    return t;
}

ULONG Refs(IUnknown* u) {
    u->AddRef();

    return u->Release();
}

std::vector<Rs7Image> Expected(const std::vector<Rs7Image>& stock, const Rs7SwapCfg& cfg = kCfg) {
    return Rs7Ramp_Build(stock, cfg.rgb).levels;
}

void Add(IDirect3DTexture9* t, Rs7Family f = Rs7Family::Note, const char* path = "test/note_gradient_map.dds") {
    Rs7Cand_Add(t, f, path, 0x1000);
}
}  // namespace

TEST(iid_texture9_matches_the_sdk) {
    CHECK(IsEqualGUID(kRs7IID_Texture9, IID_IDirect3DTexture9));
}

TEST(thunk_prologue_is_hotpatchable) {
    const uint8_t want[5] = {0x8B, 0xFF, 0x55, 0x8B, 0xEC};

    CHECK(std::memcmp(reinterpret_cast<const void*>(&Rs7_Thunk_SetTexture), want, 5) == 0);
    CHECK(std::memcmp(reinterpret_cast<const void*>(&Rs7_Thunk_GetTexture), want, 5) == 0);
}

TEST(d3d_install_patches_slots_64_and_65_and_is_idempotent) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Begin();
    void** vt = *reinterpret_cast<void***>(d.dev);
    void* const o64 = vt[64];
    void* const o65 = vt[65];
    HMODULE m = nullptr;

    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(o65), &m);
    CHECK(m == GetModuleHandleW(L"d3d9.dll"));  // the slot is d3d9's SetTexture before we touch it
    Capturing cap;

    CHECK(Rs7Swap_InstallOn(d.dev));
    CHECK(vt[64] == reinterpret_cast<void*>(&Rs7_Thunk_GetTexture));
    CHECK(vt[65] == reinterpret_cast<void*>(&Rs7_Thunk_SetTexture));
    CHECK(Logged("-> hooked; RSMods (xinput1_3.dll) absent", "(body d3d9.dll)"));
    CHECK(Rs7Swap_InstallOn(d.dev));
    CHECK(vt[64] == reinterpret_cast<void*>(&Rs7_Thunk_GetTexture));
    CHECK_EQ(Lines().size(), size_t(1));  // the second install changes and logs nothing
    End(d.dev);
    CHECK(vt[64] == o64 && vt[65] == o65);
}

TEST(d3d_swap_binds_our_copy_and_turns_v6_on) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Begin();
    const auto lv = Stock(1);
    IDirect3DTexture9* stock = rs7d3d::MakeTex(d.dev, lv);

    CHECK(stock != nullptr);
    if (!stock) {
        return;
    }

    Add(stock);
    CHECK(Rs7Swap_InstallOn(d.dev));
    Capturing cap;

    d.dev->SetTexture(1, stock);
    IDirect3DBaseTexture9* raw = RawBound(d.dev, 1);

    CHECK(raw != nullptr && raw != static_cast<IDirect3DBaseTexture9*>(stock));
    IDirect3DTexture9* ours = nullptr;

    if (raw) {
        raw->QueryInterface(IID_IDirect3DTexture9, reinterpret_cast<void**>(&ours));
    }
    CHECK(ours != nullptr);

    if (ours) {
        D3DSURFACE_DESC a{}, b{};

        stock->GetLevelDesc(0, &a);
        ours->GetLevelDesc(0, &b);
        CHECK(a.Width == b.Width && a.Height == b.Height && a.Format == b.Format && b.Pool == D3DPOOL_MANAGED);
        CHECK_EQ(ours->GetLevelCount(), stock->GetLevelCount());
        CHECK(rs7d3d::SameImages(rs7d3d::ReadTex(ours), Expected(lv)));
        ours->Release();
    }

    CHECK(V6On());
    CHECK(Rs7Cand_Find(stock) && Rs7Cand_Find(stock)->state == Rs7CandState::Active);
    CHECK(Logged("ramp: process note", "fmt=22 256x128 levels=1 pool=1"));
    CHECK(Logged("ramp: built", "self-check exact=1 v6-pure=1 changed-lines=4"));
    CHECK(Logged("ramp: V6 ON"));
    End(d.dev);
    CHECK_EQ(stock->Release(), 0ul);  // every reference we took is gone again
}

TEST(d3d_gettexture_hides_the_swap) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Begin();
    IDirect3DTexture9* stock = rs7d3d::MakeTex(d.dev, Stock(1));
    IDirect3DTexture9* other = rs7d3d::MakeTex(d.dev, Stock(1));

    Add(stock);
    Rs7Swap_InstallOn(d.dev);
    d.dev->SetTexture(1, stock);
    IDirect3DBaseTexture9* t = nullptr;

    CHECK(SUCCEEDED(d.dev->GetTexture(1, &t)));
    CHECK(t == static_cast<IDirect3DBaseTexture9*>(stock));
    if (t) {
        t->Release();
    }
    CHECK(Has(Stats(), "hidden=1"));
    d.dev->SetTexture(1, other);  // not a candidate
    t = nullptr;
    d.dev->GetTexture(1, &t);
    CHECK(t == static_cast<IDirect3DBaseTexture9*>(other));
    if (t) {
        t->Release();
    }
    CHECK(Has(Stats(), "hidden=1"));
    End(d.dev);
    other->Release();
    CHECK_EQ(stock->Release(), 0ul);
}

TEST(d3d_levels_1_4_9_are_copied_exactly) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    for (unsigned levels : {1u, 4u, 9u}) {
        Begin();
        const auto lv = Stock(levels);
        IDirect3DTexture9* stock = rs7d3d::MakeTex(d.dev, lv);

        CHECK(stock && stock->GetLevelCount() == levels);
        if (!stock) {
            continue;
        }

        Add(stock);
        Rs7Swap_InstallOn(d.dev);
        d.dev->SetTexture(0, stock);
        Rs7Cand* c = Rs7Cand_Find(stock);

        CHECK(c && c->state == Rs7CandState::Active);
        if (c && c->ours) {
            CHECK(rs7d3d::SameImages(rs7d3d::ReadTex(static_cast<IDirect3DTexture9*>(c->ours)), Expected(lv)));
        }
        CHECK(rs7d3d::SameImages(rs7d3d::ReadTex(stock), lv));  // the stock itself is untouched
        End(d.dev);
        stock->Release();
    }
}

TEST(d3d_default_pool_stock_is_refused_and_v6_stays_off) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Begin();
    IDirect3DTexture9* stock = rs7d3d::MakeTex(d.dev, Stock(1), D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT);

    CHECK(stock != nullptr);
    if (!stock) {
        return;
    }

    IDirect3DTexture9* note = rs7d3d::MakeTex(d.dev, Stock(1));

    Add(stock);
    Add(note, Rs7Family::Note, "other/note_gradient_map.dds");
    Rs7Swap_InstallOn(d.dev);
    Capturing cap;

    d.dev->SetTexture(1, note);  // an active note alone would turn V6 on
    d.dev->SetTexture(1, stock);
    CHECK(RawBound(d.dev, 1) == static_cast<IDirect3DBaseTexture9*>(stock));
    CHECK(Rs7Cand_Find(stock)->state == Rs7CandState::Refused);
    CHECK(Logged("ramp: refuse note", "pool"));
    CHECK(V6Off());
    End(d.dev);
    note->Release();
    CHECK_EQ(stock->Release(), 0ul);
}

TEST(d3d_dxt1_refused_unless_allow_inexact) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    IDirect3DTexture9* stock = nullptr;

    if (FAILED(d.dev->CreateTexture(256, 128, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &stock, nullptr))) {
        CHECK(!"DXT1 texture");
        return;
    }

    std::vector<uint8_t> blocks;
    D3DLOCKED_RECT lr{};

    if (SUCCEEDED(stock->LockRect(0, &lr, nullptr, 0))) {
        for (unsigned by = 0; by < 32; ++by) {
            for (unsigned bx = 0; bx < 64; ++bx) {
                uint8_t* b = static_cast<uint8_t*>(lr.pBits) + by * lr.Pitch + bx * 8;
                const uint16_t c0 = uint16_t(0xF800 | (bx << 5)), c1 = uint16_t(0x001F | (by << 6));

                std::memcpy(b, &c0, 2);
                std::memcpy(b + 2, &c1, 2);
                const uint32_t idx = 0xE4E4E4E4u ^ (bx * 0x01010101u);

                std::memcpy(b + 4, &idx, 4);
            }
        }

        blocks.resize(size_t(lr.Pitch) * 32);
        std::memcpy(blocks.data(), lr.pBits, blocks.size());
        const INT pitch = lr.Pitch;

        stock->UnlockRect(0);
        lr.Pitch = pitch;
    }

    std::vector<Rs7Image> decoded(1);

    decoded[0].w = 256;
    decoded[0].h = 128;
    decoded[0].px.resize(256 * 128);
    CHECK(Rs7_DecodeDxt(blocks.data(), blocks.size(), size_t(lr.Pitch), 256, 128, false, decoded[0].px.data()));

    Begin();
    Add(stock);
    Rs7Swap_InstallOn(d.dev);

    {
        Capturing cap;

        d.dev->SetTexture(0, stock);
        CHECK(Rs7Cand_Find(stock)->state == Rs7CandState::Refused);
        CHECK(Logged("ramp: refuse note", "allow_inexact"));
        CHECK(RawBound(d.dev, 0) == static_cast<IDirect3DBaseTexture9*>(stock));
    }

    End(d.dev);

    Rs7SwapCfg inexact = kCfg;

    inexact.allowInexact = true;
    Begin(inexact);
    Add(stock);
    Rs7Swap_InstallOn(d.dev);
    d.dev->SetTexture(0, stock);
    Rs7Cand* c = Rs7Cand_Find(stock);

    CHECK(c && c->state == Rs7CandState::Active);

    if (c && c->ours) {
        auto* ours = static_cast<IDirect3DTexture9*>(c->ours);
        D3DSURFACE_DESC od{};

        ours->GetLevelDesc(0, &od);
        CHECK(od.Format == D3DFMT_A8R8G8B8);
        CHECK(rs7d3d::SameImages(rs7d3d::ReadTex(ours), Expected(decoded, inexact)));
    }

    CHECK(V6On());
    End(d.dev);
    CHECK_EQ(stock->Release(), 0ul);
}

TEST(d3d_identical_stocks_share_one_copy) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Begin();
    IDirect3DTexture9* a = rs7d3d::MakeTex(d.dev, Stock(2));
    IDirect3DTexture9* b = rs7d3d::MakeTex(d.dev, Stock(2));

    Add(a);
    Add(b, Rs7Family::Note, "tail/note_gradient_map.dds");
    Rs7Swap_InstallOn(d.dev);
    d.dev->SetTexture(0, a);
    d.dev->SetTexture(1, b);
    CHECK(Rs7Cand_Find(a)->ours != nullptr && Rs7Cand_Find(a)->ours == Rs7Cand_Find(b)->ours);
    CHECK(RawBound(d.dev, 0) == RawBound(d.dev, 1));
    End(d.dev);
    CHECK_EQ(a->Release(), 0ul);
    CHECK_EQ(b->Release(), 0ul);
}

// Overlay: [color] string6_color live. The next SetTexture on the hooked device repaints the live copy
// with the new colours, exactly as a first build with them would; unused cached copies are dropped.
TEST(d3d_recolour_repaints_the_live_copy) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Begin();
    const auto lv = Stock(4);
    IDirect3DTexture9* a = rs7d3d::MakeTex(d.dev, lv);

    Add(a);
    Rs7Swap_InstallOn(d.dev);
    d.dev->SetTexture(1, a);
    IDirect3DBaseTexture9* raw = RawBound(d.dev, 1);

    CHECK(raw != nullptr && raw != static_cast<IDirect3DBaseTexture9*>(a));
    const Rs7SwapCfg next = {{0x112233, 0xFF8000}, false};

    Rs7Swap_RequestRecolour(next.rgb);
    Capturing cap;

    d.dev->SetTexture(2, nullptr);  // any bind on the render thread runs it
    CHECK(Logged("ramp: recoloured 1 copies to 112233 / FF8000 (0 failed"));
    IDirect3DTexture9* ours = nullptr;

    if (raw) {
        raw->QueryInterface(IID_IDirect3DTexture9, reinterpret_cast<void**>(&ours));
    }
    CHECK(ours != nullptr);

    if (ours) {
        CHECK(rs7d3d::SameImages(rs7d3d::ReadTex(ours), Expected(lv, next)));
        CHECK(!rs7d3d::SameImages(rs7d3d::ReadTex(ours), Expected(lv)));
        ours->Release();
    }

    CHECK(V6On());
    Rs7SwapStats st{};

    Rs7Swap_Stats(&st);
    CHECK(st.installed && st.active == 1 && st.copies == 1 && st.rgb[0] == 0x112233u && st.rgb[1] == 0xFF8000u);
    // The same colours again: nothing to do.
    Lines().clear();
    Rs7Swap_RequestRecolour(next.rgb);
    d.dev->SetTexture(2, nullptr);
    CHECK(!Logged("ramp: recoloured"));
    // A copy built after the change uses the new colours too.
    IDirect3DTexture9* b = rs7d3d::MakeTex(d.dev, Stock(2));

    Add(b, Rs7Family::Note, "tail/note_gradient_map.dds");
    d.dev->SetTexture(3, b);
    IDirect3DTexture9* oursB = nullptr;

    if (IDirect3DBaseTexture9* rb = RawBound(d.dev, 3)) {
        rb->QueryInterface(IID_IDirect3DTexture9, reinterpret_cast<void**>(&oursB));
    }
    CHECK(oursB != nullptr);

    if (oursB) {
        CHECK(rs7d3d::SameImages(rs7d3d::ReadTex(oursB), Expected(Stock(2), next)));
        oursB->Release();
    }

    End(d.dev);
    CHECK_EQ(a->Release(), 0ul);
    CHECK_EQ(b->Release(), 0ul);
}

// d3d9.dll 10.0.26100 gives each device its own vtable copy (heap, read-write), so the slot patch reaches
// only the hooked device. A runtime with one shared class vtable would send other devices through the
// hook as well: that path is driven here by calling the thunk directly.
TEST(d3d_install_reaches_only_the_hooked_device) {
    Device d1, d2;
    if (!rs7d3d::Have(d1) || !rs7d3d::Have(d2)) {
        return;
    }

    void** vt2 = *reinterpret_cast<void***>(d2.dev);
    void* const o65 = vt2[65];

    Begin();
    CHECK(Rs7Swap_InstallOn(d1.dev));

    if (*reinterpret_cast<void**>(d1.dev) != *reinterpret_cast<void**>(d2.dev)) {
        std::printf("  d3d9: per-device vtables (the hook reaches only the hooked device)\n");
        CHECK(vt2[65] == o65);
    } else {
        std::printf("  d3d9: one shared device vtable\n");
    }

    End(d1.dev);
}

TEST(d3d_other_device_passes_through) {
    using SetTex = HRESULT(__stdcall*)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
    const auto hook = reinterpret_cast<SetTex>(&Rs7_Thunk_SetTexture);
    Device d1, d2;
    if (!rs7d3d::Have(d1) || !rs7d3d::Have(d2)) {
        return;
    }

    Begin();
    IDirect3DTexture9* s1 = rs7d3d::MakeTex(d1.dev, Stock(1));
    IDirect3DTexture9* s2 = rs7d3d::MakeTex(d2.dev, Stock(1));

    Add(s1);
    Add(s2, Rs7Family::Note, "dev2/note_gradient_map.dds");
    Rs7Swap_InstallOn(d1.dev);
    d1.dev->SetTexture(1, s1);
    CHECK(V6On());
    Capturing cap;

    CHECK(SUCCEEDED(hook(d2.dev, 1, s2)));  // as if d2 shared the hooked vtable
    CHECK(RawBound(d2.dev, 1) == static_cast<IDirect3DBaseTexture9*>(s2));
    CHECK(Rs7Cand_Find(s2)->state == Rs7CandState::Pending);
    CHECK(V6Off());
    CHECK(Logged("ramp: candidate", "pass-through, swapping off"));
    d1.dev->SetTexture(1, s1);  // the hooked device still binds its copy, V6 stays off (sticky)
    CHECK(RawBound(d1.dev, 1) != static_cast<IDirect3DBaseTexture9*>(s1));
    CHECK(V6Off());
    d2.dev->SetTexture(1, nullptr);
    End(d1.dev);
    s2->Release();
    CHECK_EQ(s1->Release(), 0ul);
}

TEST(d3d_recheck_rehooks_only_when_d3d9_restored_the_slots) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Begin();
    void** vt = *reinterpret_cast<void***>(d.dev);
    void* const o64 = vt[64];
    void* const o65 = vt[65];

    Rs7Swap_InstallOn(d.dev);
    Capturing cap;

    Rs7Swap_Recheck();  // ours: nothing to do
    CHECK(Lines().empty());
    vt[64] = o64;  // the runtime rewrote its copy
    vt[65] = o65;
    Rs7Swap_Recheck();
    CHECK(vt[65] == reinterpret_cast<void*>(&Rs7_Thunk_SetTexture) &&
          vt[64] == reinterpret_cast<void*>(&Rs7_Thunk_GetTexture));
    CHECK(Logged("slots were reset", "hooked again"));
    void* const foreign = reinterpret_cast<void*>(&Rs7_Thunk_GetTexture);

    vt[65] = foreign;  // someone else's hook: left alone
    Rs7Swap_Recheck();
    CHECK(vt[65] == foreign);
    CHECK(Logged("not ours, not d3d9's"));
    vt[65] = reinterpret_cast<void*>(&Rs7_Thunk_SetTexture);
    End(d.dev);
    CHECK(vt[64] == o64 && vt[65] == o65);
}

TEST(d3d_hook_survives_a_fault) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Begin();
    IDirect3DTexture9* stock = rs7d3d::MakeTex(d.dev, Stock(1));

    Add(stock);
    Rs7Swap_InstallOn(d.dev);
    Rs7Swap_InjectFaultForTests(true);
    Capturing cap;

    CHECK(SUCCEEDED(d.dev->SetTexture(1, stock)));
    CHECK(RawBound(d.dev, 1) == static_cast<IDirect3DBaseTexture9*>(stock));
    CHECK(Has(Stats(), "faults=1"));
    CHECK(Rs7Cand_Find(stock)->state == Rs7CandState::Refused);
    CHECK(V6Off());
    Rs7Swap_InjectFaultForTests(false);
    d.dev->SetTexture(1, stock);  // refused: pass-through from now on, no second fault
    CHECK(RawBound(d.dev, 1) == static_cast<IDirect3DBaseTexture9*>(stock));
    CHECK(Has(Stats(), "faults=1"));
    End(d.dev);
    CHECK_EQ(stock->Release(), 0ul);
}

// ---- Batch B review: stale candidates -----------------------------------------------------------

TEST(d3d_stale_candidate_is_retired_not_swapped) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Begin();
    IDirect3DTexture9* stock = rs7d3d::MakeTex(d.dev, Stock(1));

    Add(stock);
    Rs7Swap_InstallOn(d.dev);
    g_verdict = 0;  // the engine no longer holds this pointer (freed, address reused)
    Capturing cap;

    d.dev->SetTexture(1, stock);
    CHECK(RawBound(d.dev, 1) == static_cast<IDirect3DBaseTexture9*>(stock));
    CHECK(Rs7Cand_Find(stock) == nullptr);
    CHECK(Logged("ramp: drop stale note"));
    CHECK(V6Off());
    CHECK_EQ(Refs(stock), 1ul);  // no reference taken
    End(d.dev);
    stock->Release();
}

TEST(d3d_busy_list_lock_leaves_it_pending_until_the_next_bind) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Begin();
    IDirect3DTexture9* stock = rs7d3d::MakeTex(d.dev, Stock(1));

    Add(stock);
    Rs7Swap_InstallOn(d.dev);
    g_verdict = -1;
    d.dev->SetTexture(1, stock);
    CHECK(RawBound(d.dev, 1) == static_cast<IDirect3DBaseTexture9*>(stock));
    CHECK(Rs7Cand_Find(stock)->state == Rs7CandState::Pending);
    g_verdict = 1;
    d.dev->SetTexture(1, stock);
    CHECK(Rs7Cand_Find(stock)->state == Rs7CandState::Active);
    CHECK(V6On());
    End(d.dev);
    stock->Release();
}

TEST(d3d_gone_active_candidate_is_released_on_the_render_thread) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Begin();
    IDirect3DTexture9* stock = rs7d3d::MakeTex(d.dev, Stock(1));
    IDirect3DTexture9* other = rs7d3d::MakeTex(d.dev, Stock(1));

    Add(stock);
    Rs7Swap_InstallOn(d.dev);
    d.dev->SetTexture(1, stock);
    CHECK(V6On() && Refs(stock) == 2ul);  // ours + the candidate's
    d.dev->SetTexture(1, other);
    Rs7TexWalk empty;

    Rs7Ramp_Reconcile(empty);  // one miss: kept
    CHECK(Rs7Cand_Find(stock) && Rs7Cand_Find(stock)->gone == 1);
    Rs7TexWalk listed;
    Rs7TexFound f{};

    f.d3d = reinterpret_cast<uintptr_t>(stock);
    f.family = Rs7Family::Note;
    listed.found.push_back(f);
    Rs7Ramp_Reconcile(listed);  // listed again: the count restarts
    CHECK(Rs7Cand_Find(stock)->gone == 0);
    Rs7Ramp_Reconcile(empty);
    Rs7Ramp_Reconcile(empty);  // two misses in a row: released at the next SetTexture
    CHECK(Rs7Cand_Find(stock) != nullptr && Refs(stock) == 2ul);
    Capturing cap;

    d.dev->SetTexture(2, nullptr);
    CHECK(Rs7Cand_Find(stock) == nullptr);
    CHECK_EQ(Refs(stock), 1ul);
    CHECK(Logged("ramp: release note", "gone from the engine list"));
    CHECK(V6Off());  // no active note any more
    End(d.dev);
    other->Release();
    CHECK_EQ(stock->Release(), 0ul);
}

TEST(d3d_refused_candidate_gone_from_the_list_unblocks_v6) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Begin();
    IDirect3DTexture9* note = rs7d3d::MakeTex(d.dev, Stock(1));
    IDirect3DTexture9* bad = rs7d3d::MakeTex(d.dev, Stock(1), D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT);

    Add(note);
    Add(bad, Rs7Family::Additive, "stem/gradient_map_additive.dds");
    Rs7Swap_InstallOn(d.dev);
    d.dev->SetTexture(0, note);
    d.dev->SetTexture(1, bad);
    CHECK(V6Off());  // a refused additive keeps V6 off (D6)
    Rs7TexWalk w;
    Rs7TexFound f{};

    f.d3d = reinterpret_cast<uintptr_t>(note);
    f.family = Rs7Family::Note;
    w.found.push_back(f);
    Capturing cap;

    Rs7Ramp_Reconcile(w);  // the refused one is gone from the engine list
    CHECK(Rs7Cand_Find(bad) == nullptr);
    CHECK(Logged("ramp: candidate gone: additive", "(refused), slot freed"));
    CHECK(V6On());
    End(d.dev);
    bad->Release();
    note->Release();
}
