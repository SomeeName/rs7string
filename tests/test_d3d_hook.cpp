// The vtable hook: install and recheck of slots 64/65, GetTexture hiding the swap, other devices, faults.
// clang-format off: initguid.h must come before d3d9.h (sorting would put it after)
#include <windows.h>
#include <initguid.h>   // this file only: defines the SDK's IIDs for iid_texture9_matches_the_sdk
#include <d3d9.h>
// clang-format on
#include "d3d/iids.h"
#include "d3d/swap.h"
#include "swap_fixture.h"
#include "test.h"
#include <cstring>

using namespace rs7swapfix;

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
