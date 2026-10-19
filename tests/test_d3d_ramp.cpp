// The stock ramps: swap and copy, refusals, sharing, recolour, stale and gone candidates.
#include "d3d/swap.h"
#include "d3d/worker.h"
#include "features/colours/colours.h"
#include "platform/dxt.h"
#include "swap_fixture.h"
#include "test.h"
#include <cstring>
#include <vector>

using namespace rs7swapfix;

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
    CHECK(Rs7_DecodeDxt(blocks.data(), blocks.size(), size_t(lr.Pitch), 256, 128, false, decoded[0].px.data(),
                        decoded[0].px.size()));

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
