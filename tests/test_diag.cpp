#include "core/diag.h"
#include "core/mode.h"
#include "log.h"
#include "test.h"
#include <windows.h>
#include <cstring>
#include <string>
#include <vector>

namespace {
std::vector<std::string>& Lines() {
    static std::vector<std::string> v;

    return v;
}

void Capture(const char* msg) {
    Lines().push_back(msg);
}

// Captures Rs7Log lines for one test and starts with a fresh diagnostics budget.
struct Capturing {
    Capturing() {
        Lines().clear();
        Rs7Diag_Reset();
        Rs7Log_SetSink(Capture);
    }

    ~Capturing() { Rs7Log_SetSink(nullptr); }
};

uint32_t Bits(float f) {
    uint32_t u;

    std::memcpy(&u, &f, 4);

    return u;
}

template <class T>
void Put(std::vector<uint8_t>& b, size_t off, T v) {
    std::memcpy(&b[off], &v, sizeof v);
}
}  // namespace

// ---- rate limiter ---------------------------------------------------------------------------

TEST(diag_take_allows_8_hits_per_point_and_string) {
    Rs7Diag_Reset();

    for (int i = 0; i < kRs7DiagMaxHits; ++i) {
        CHECK(Rs7Diag_Take(kRs7D1, 6));
    }

    CHECK(!Rs7Diag_Take(kRs7D1, 6));
    CHECK(!Rs7Diag_Take(kRs7D1, 6));
    CHECK(Rs7Diag_Take(kRs7D1, 0));  // string 0 has its own budget
    CHECK(Rs7Diag_Take(kRs7D2, 6));  // so has every other log point
}

TEST(diag_take_ignores_strings_other_than_0_and_6) {
    Rs7Diag_Reset();
    CHECK(!Rs7Diag_Take(kRs7D1, 1));
    CHECK(!Rs7Diag_Take(kRs7D1, 5));
    CHECK(!Rs7Diag_Take(kRs7D1, 7));
    CHECK(!Rs7Diag_Take(kRs7D1, 0xFF));
    CHECK(!Rs7Diag_Take(kRs7DiagPointCount, 6));  // unknown point
    CHECK(!Rs7Diag_Take(-1, 6));
}

TEST(diag_reset_restores_the_budget) {
    Rs7Diag_Reset();

    for (int i = 0; i < 20; ++i) {
        Rs7Diag_Take(kRs7D5, 0);
    }

    CHECK(!Rs7Diag_Take(kRs7D5, 0));
    Rs7Diag_Reset();
    CHECK(Rs7Diag_Take(kRs7D5, 0));
}

TEST(song_setup_resets_the_diag_budget) {
    Rs7Diag_Reset();

    for (int i = 0; i < 20; ++i) {
        Rs7Diag_Take(kRs7D3, 6);
    }

    CHECK(!Rs7Diag_Take(kRs7D3, 6));
    std::vector<uint8_t> det(0x13B8);  // detector without owner: no matcher

    Put<int>(det, rs7::kDetInstrument, 1);
    std::vector<int16_t> tuning(7, 0);
    const Rs7Int16Vec v{tuning.data(), tuning.data() + tuning.size()};

    Rs7_OnSongTuning(nullptr, det.data(), &v);
    CHECK(Rs7Diag_Take(kRs7D3, 6));
}

// ---- log lines (floats as raw IEEE bits) ----------------------------------------------------

TEST(d1_note_head_line) {
    Capturing cap;
    std::vector<uint8_t> row(0x100);

    Put(row, 0x44, Bits(5.02f));
    Put(row, 0x90, Bits(3.412f));
    Rs7_DiagNoteHead(6, 7, 0, row.data(), Bits(-4.99f), Bits(6.628f), Bits(11.618f));
    CHECK_EQ(Lines().size(), size_t(1));
    if (!Lines().empty()) {
        CHECK_EQ(Lines()[0], std::string("D1 head lane: string 6 fret 7 open 0 z0 40A0A3D7 z1 405A5E35 "
                                         "z 40D41893 floor C09FAE14 |Cz| 4139E354"));
    }
}

TEST(d1_skips_other_strings_and_stops_after_8) {
    Capturing cap;
    std::vector<uint8_t> row(0x100);

    Rs7_DiagNoteHead(3, 7, 0, row.data(), 0, 0, 0);
    CHECK_EQ(Lines().size(), size_t(0));

    for (int i = 0; i < 12; ++i) {
        Rs7_DiagNoteHead(0, 7, 0, row.data(), 0, 0, 0);
    }

    CHECK_EQ(Lines().size(), size_t(8));
}

TEST(d2_gem_create_line_masks_byte_arguments) {
    Capturing cap;

    Rs7_DiagGemCreate(0x0079E520, 0x569A10B6, 0x0BADF00D, 0xABCDEF07, 0xABCDEF06, 2);
    Rs7_DiagGemCreate(0x0079E520, 0x569A10B6, 0x0BADF00D, 7, 0xFF, 2);  // fret digit: string 0xFF
    CHECK_EQ(Lines().size(), size_t(1));
    if (!Lines().empty()) {
        CHECK_EQ(Lines()[0], std::string("D2 gem create: string 6 fret 7 type 2 model 569A10B6 note 0BADF00D "
                                         "caller 0079E520"));
    }
}

TEST(d3_geom_build_line) {
    Capturing cap;
    std::vector<uint8_t> gem(0x80), geom(0x50);

    gem[0xC] = 6;
    gem[0xD] = 7;
    Put<uint32_t>(gem, 0x10, 2);
    Put<const void*>(geom, 0x8, gem.data());
    Put<uint32_t>(geom, 0x14, 2);

    for (uint32_t i = 0; i < 9; ++i) {
        Put<uint32_t>(geom, 0x20 + 4 * i, 0x3F800000 + i);
    }

    Rs7_DiagGeomBuild(geom.data());
    char want[256];

    std::snprintf(want, sizeof want,
                  "D3 geom build: string 6 fret 7 type 2 mode 2 gem %08X geom %08X v20 3F800000 3F800001 "
                  "3F800002 v2C 3F800003 3F800004 3F800005 v38 3F800006 3F800007 3F800008",
                  unsigned(reinterpret_cast<uintptr_t>(gem.data())),
                  unsigned(reinterpret_cast<uintptr_t>(geom.data())));
    CHECK_EQ(Lines().size(), size_t(1));
    if (!Lines().empty()) {
        CHECK_EQ(Lines()[0], std::string(want));
    }
}

TEST(d3_unreadable_gem_logs_nothing_and_does_not_crash) {
    Capturing cap;
    std::vector<uint8_t> geom(0x50);

    Put<uintptr_t>(geom, 0x8, 0x10);  // unmapped
    Rs7_DiagGeomBuild(geom.data());
    Rs7_DiagGeomBuild(reinterpret_cast<const uint8_t*>(0x20));
    CHECK_EQ(Lines().size(), size_t(0));
}

TEST(d4_string_6_logs_the_stub_result) {
    Capturing cap;
    const uint32_t out[3] = {Bits(1.0f), Bits(2.0f), Bits(-3.0f)};

    Rs7_DiagCellPos(0x007A1E40, nullptr, 0x12345607, 1, 0xABCDEF06, out);
    CHECK_EQ(Lines().size(), size_t(1));
    if (!Lines().empty()) {
        CHECK_EQ(Lines()[0], std::string("D4 cell pos: string 6 fret 7 k 1 out 3F800000 40000000 C0400000 "
                                         "caller 007A1E40"));
    }
}

TEST(d4_string_0_reads_the_stock_cell_from_the_grid) {
    Capturing cap;
    std::vector<uint8_t> grid(26 * 0x1CC);
    const size_t p = 0x30 + 6 * 0x1CC + 2 * 0xC;  // fret 7, k 2, string 0

    Put(grid, p, Bits(4.0f));
    Put(grid, p + 4, Bits(5.0f));
    Put(grid, p + 8, Bits(6.0f));
    Rs7_DiagCellPos(0x007A1E40, grid.data(), 7, 2, 0, nullptr);
    Rs7_DiagCellPos(0x007A1E40, grid.data(), 0, 2, 0, nullptr);  // fret 0: stock fails, nothing to log
    CHECK_EQ(Lines().size(), size_t(1));
    if (!Lines().empty()) {
        CHECK_EQ(Lines()[0], std::string("D4 cell pos: string 0 fret 7 k 2 out 40800000 40A00000 40C00000 "
                                         "caller 007A1E40"));
    }
}

TEST(d5_gem_pos_line) {
    Capturing cap;
    std::vector<uint8_t> gem(0x80);

    gem[0xC] = 0;
    gem[0xD] = 5;
    Put<uint32_t>(gem, 0x10, 2);
    Put(gem, 0x14, Bits(0.5f));
    Put(gem, 0x18, Bits(1.5f));
    Put(gem, 0x1C, Bits(2.5f));
    const uint32_t pos[3] = {Bits(10.0f), Bits(20.0f), Bits(30.0f)};

    Rs7_DiagGemPos(gem.data(), pos);
    char want[256];

    std::snprintf(want, sizeof want,
                  "D5 gem pos: string 0 fret 5 type 2 gem %08X offset 3F000000 3FC00000 40200000 "
                  "pos 41200000 41A00000 41F00000",
                  unsigned(reinterpret_cast<uintptr_t>(gem.data())));
    CHECK_EQ(Lines().size(), size_t(1));
    if (!Lines().empty()) {
        CHECK_EQ(Lines()[0], std::string(want));
    }
}

// ---- budget peek before game-memory reads, SafeCopy filter (final review F2b/F2c) -----------

namespace {
// Two pages: `rw` readable, `na` (= rw + 4096) PAGE_NOACCESS.
struct TwoPages {
    uint8_t* rw = nullptr;
    uint8_t* na = nullptr;

    TwoPages() {
        rw = static_cast<uint8_t*>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        na = rw + 4096;
        DWORD old = 0;

        VirtualProtect(na, 4096, PAGE_NOACCESS, &old);
    }

    ~TwoPages() { VirtualFree(rw, 0, MEM_RELEASE); }
};

// Counts access violations at addresses in [g_avLo, g_avLo + 4096): every read the loggers attempt
// there (SafeCopy then swallows it). A vectored handler sees the exception before any frame does.
volatile LONG g_avCount = 0;
uintptr_t g_avLo = 0;

LONG CALLBACK CountAv(EXCEPTION_POINTERS* e) {
    const EXCEPTION_RECORD* r = e->ExceptionRecord;

    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2 &&
        r->ExceptionInformation[1] - g_avLo < 4096) {
        InterlockedIncrement(&g_avCount);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

struct AvCounter {
    void* h;

    explicit AvCounter(const void* page) {
        g_avLo = reinterpret_cast<uintptr_t>(page);
        g_avCount = 0;
        h = AddVectoredExceptionHandler(1, CountAv);
    }

    ~AvCounter() { RemoveVectoredExceptionHandler(h); }

    LONG Count() const { return g_avCount; }
};

void Spend(int point, uint32_t string) {
    while (Rs7Diag_Take(point, string)) {
    }
}

// Rs7_DiagGemPos inside __try: true if an exception with `code` reached this frame.
bool GemPosRaises(const uint8_t* gem, const uint32_t* pos, DWORD code) {
    __try {
        Rs7_DiagGemPos(gem, pos);
        return false;
    } __except (GetExceptionCode() == code ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
}
}  // namespace

TEST(diag_peek_does_not_consume) {
    Rs7Diag_Reset();

    for (int i = 0; i < 3 * kRs7DiagMaxHits; ++i) {
        CHECK(Rs7Diag_Peek(kRs7D4, 6));
    }

    Spend(kRs7D4, 6);
    CHECK(!Rs7Diag_Peek(kRs7D4, 6));
    CHECK(Rs7Diag_Peek(kRs7D4, 0));
    CHECK(!Rs7Diag_Peek(kRs7D4, 3));
    CHECK(!Rs7Diag_Peek(kRs7DiagPointCount, 6));
}

TEST(d5_spent_budget_reads_no_game_memory) {
    Capturing cap;
    TwoPages pg;
    const uint32_t pos[3] = {};
    AvCounter av(pg.na);

    Spend(kRs7D5, 0);
    Spend(kRs7D5, 6);
    Rs7_DiagGemPos(pg.na, pos);
    CHECK_EQ(av.Count(), 0);
    Rs7Diag_Reset();
    Rs7_DiagGemPos(pg.na, pos);  // with budget it does read (and SafeCopy swallows the AV)
    CHECK(av.Count() >= 1);
    CHECK_EQ(Lines().size(), size_t(0));
}

TEST(d5_spent_string_stops_before_the_remaining_reads) {
    Capturing cap;
    TwoPages pg;
    uint8_t* gem = pg.na - 0x10;  // +0xC string/fret readable, +0x10 type onwards not

    gem[0xC] = 0;
    gem[0xD] = 5;
    const uint32_t pos[3] = {};
    AvCounter av(pg.na);

    Spend(kRs7D5, 0);  // string 6 still has budget: the string byte must be read
    Rs7_DiagGemPos(gem, pos);
    CHECK_EQ(av.Count(), 0);
    Rs7Diag_Reset();
    Rs7_DiagGemPos(gem, pos);
    CHECK(av.Count() >= 1);
    CHECK(Rs7Diag_Peek(kRs7D5, 0));  // a failed read takes nothing from the budget

    for (int i = 0; i < kRs7DiagMaxHits; ++i) {
        CHECK(Rs7Diag_Take(kRs7D5, 0));
    }
}

TEST(d3_spent_budget_reads_no_game_memory) {
    Capturing cap;
    TwoPages pg;
    AvCounter av(pg.na);

    Spend(kRs7D3, 0);
    Spend(kRs7D3, 6);
    Rs7_DiagGeomBuild(pg.na);
    CHECK_EQ(av.Count(), 0);

    // String 6 spent, string 0 not: geom +0x8 and the gem are read, geom +0x14 (in `na`) is not.
    Rs7Diag_Reset();
    Spend(kRs7D3, 6);
    std::vector<uint8_t> gem(0x80);

    gem[0xC] = 6;
    uint8_t* geom = pg.na - 0x10;
    const uint8_t* g = gem.data();

    std::memcpy(geom + 0x8, &g, sizeof g);
    Rs7_DiagGeomBuild(geom);
    CHECK_EQ(av.Count(), 0);
    Rs7Diag_Reset();
    Rs7_DiagGeomBuild(geom);
    CHECK(av.Count() >= 1);
    CHECK_EQ(Lines().size(), size_t(0));
}

TEST(d4_spent_budget_reads_no_game_memory) {
    Capturing cap;
    TwoPages pg;
    AvCounter av(pg.na);

    Spend(kRs7D4, 6);
    Spend(kRs7D4, 0);
    Rs7_DiagCellPos(0, nullptr, 7, 0, 6, reinterpret_cast<const uint32_t*>(pg.na));  // string 6: out
    Rs7_DiagCellPos(0, pg.na, 7, 0, 0, nullptr);                                     // string 0: grid
    CHECK_EQ(av.Count(), 0);
    Rs7Diag_Reset();
    Rs7_DiagCellPos(0, nullptr, 7, 0, 6, reinterpret_cast<const uint32_t*>(pg.na));
    Rs7_DiagCellPos(0, pg.na, 7, 0, 0, nullptr);
    CHECK_EQ(av.Count(), 2);
    CHECK(Rs7Diag_Peek(kRs7D4, 0));
    CHECK(Rs7Diag_Peek(kRs7D4, 6));
    CHECK_EQ(Lines().size(), size_t(0));
}

TEST(safecopy_handles_only_access_violations) {
    Capturing cap;
    uint8_t* page =
        static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD));
    const uint32_t pos[3] = {};

    // A guard-page exception is not an access violation: it must pass through SafeCopy.
    CHECK(GemPosRaises(page, pos, EXCEPTION_GUARD_PAGE));
    VirtualFree(page, 0, MEM_RELEASE);
    // An access violation is still swallowed.
    TwoPages pg;

    CHECK(!GemPosRaises(pg.na, pos, EXCEPTION_ACCESS_VIOLATION));
}
