#include "features/arrangements/arrangements.h"
#include "features/arrangements/coop_state.h"
#include "test.h"
#include <cstring>

TEST(arrangements_alt_slots_stop_at_the_four_authored_buttons) {
    CHECK(rs7arr::AltSlotFits(0));
    CHECK(rs7arr::AltSlotFits(3));
    CHECK(!rs7arr::AltSlotFits(4));
    CHECK(!rs7arr::AltSlotFits(7));
}

TEST(arrangements_window_at_offset_0_is_the_stock_first_four) {
    // 8 records, selected 2: alternates 0 1 3 4 shown, 5 6 7 hidden.
    CHECK(rs7arr::AltInWindow(0, 2, 0));
    CHECK(rs7arr::AltInWindow(1, 2, 0));
    CHECK(!rs7arr::AltInWindow(2, 2, 0));
    CHECK(rs7arr::AltInWindow(3, 2, 0));
    CHECK(rs7arr::AltInWindow(4, 2, 0));
    CHECK(!rs7arr::AltInWindow(5, 2, 0));
}

TEST(arrangements_window_scrolls_by_alternate_rank) {
    // selected 2, offset 3: ranks 3-6 = records 4 5 6 7.
    CHECK(!rs7arr::AltInWindow(3, 2, 3));
    CHECK(rs7arr::AltInWindow(4, 2, 3));
    CHECK(rs7arr::AltInWindow(7, 2, 3));
    CHECK(!rs7arr::AltInWindow(0, 2, 3));
}

TEST(arrangements_scroll_stays_inside_the_alternates) {
    // 7 alternates: offsets 0-3.
    CHECK_EQ(rs7arr::Scroll(0, 7, true, false), 1u);
    CHECK_EQ(rs7arr::Scroll(3, 7, true, false), 3u);
    CHECK_EQ(rs7arr::Scroll(2, 7, false, true), 1u);
    CHECK_EQ(rs7arr::Scroll(0, 7, false, true), 0u);
    CHECK_EQ(rs7arr::Scroll(0, 4, true, false), 0u);  // four alternates fit
    CHECK_EQ(rs7arr::Scroll(1, 7, false, false), 1u);
}

TEST(arrangements_label_tags_count_the_hidden_alternates) {
    char t[16];

    rs7arr::LabelTag(4, 0, 7, t, sizeof t);
    CHECK(std::strcmp(t, " (+3)") == 0);
    rs7arr::LabelTag(1, 0, 7, t, sizeof t);
    CHECK(t[0] == 0);
    rs7arr::LabelTag(1, 2, 7, t, sizeof t);
    CHECK(std::strcmp(t, " (-2)") == 0);
    rs7arr::LabelTag(4, 3, 7, t, sizeof t);
    CHECK(t[0] == 0);
    rs7arr::LabelTag(2, 1, 7, t, sizeof t);
    CHECK(t[0] == 0);
    rs7arr::LabelTag(4, 0, 4, t, sizeof t);  // everything fits
    CHECK(t[0] == 0);
    rs7arr::LabelTag(0, 1, 7, t, sizeof t);  // outside a shown record
    CHECK(t[0] == 0);
}

TEST(arrangements_coop_short_lists_are_not_windowed) {
    CHECK_EQ(rs7arr::CoopWindowFor(3, rs7arr::kCoopRows, 7), 0u);
    CHECK(rs7arr::CoopInWindow(7, rs7arr::kCoopRows, 0));
    CHECK(rs7arr::CoopInWindow(7, rs7arr::kCoopRows, 5));
}

TEST(arrangements_coop_window_shows_the_selected_record) {
    // 12 records, 8 rows.
    CHECK_EQ(rs7arr::CoopWindowFor(0, 12, 3), 0u);
    CHECK_EQ(rs7arr::CoopWindowFor(0, 12, 11), 4u);
    CHECK_EQ(rs7arr::CoopWindowFor(4, 12, 1), 1u);
    CHECK_EQ(rs7arr::CoopWindowFor(9, 12, 5), 4u);   // kept inside the records, then the selection still fits
    CHECK_EQ(rs7arr::CoopWindowFor(2, 12, 99), 2u);  // no valid selection: offset kept
}

TEST(arrangements_coop_window_and_step) {
    CHECK(!rs7arr::CoopInWindow(1, 12, 2));
    CHECK(rs7arr::CoopInWindow(2, 12, 2));
    CHECK(rs7arr::CoopInWindow(9, 12, 2));
    CHECK(!rs7arr::CoopInWindow(10, 12, 2));
    CHECK_EQ(rs7arr::CoopStep(7, 12, 1), 8u);
    CHECK_EQ(rs7arr::CoopWindowFor(0, 12, 8), 1u);  // Down below the window scrolls one record
    CHECK_EQ(rs7arr::CoopStep(11, 12, 1), 11u);     // stops at the ends
    CHECK_EQ(rs7arr::CoopStep(0, 12, -1), 0u);
    CHECK_EQ(rs7arr::CoopStep(4, 12, -1), 3u);
    CHECK_EQ(rs7arr::CoopWindowFor(4, 12, 3), 3u);
    CHECK_EQ(rs7arr::CoopStep(2, 12, -5), 0u);
}

TEST(arrangements_coop_focus_index) {
    CHECK_EQ(rs7arr::CoopFocusIndex("arrangement0"), 0);
    CHECK_EQ(rs7arr::CoopFocusIndex("Buttons.arrangement11.States"), 11);
    CHECK_EQ(rs7arr::CoopFocusIndex("Buttons.arrangement.States"), -1);
    CHECK_EQ(rs7arr::CoopFocusIndex("playSong"), -1);
}

TEST(arrangements_coop_eviction_reuses_both_spare_focus_lists) {
    uint32_t lists[2][12] = {};
    rs7arr::PageState st;

    st.page = 1;
    st.offset = 4;

    for (uint32_t i = 0; i < 2; ++i) {
        st.spareFocus[3 * i] = uint32_t(uintptr_t(lists[i]));
        st.spareFocus[3 * i + 1] = uint32_t(uintptr_t(lists[i] + 6));
        st.spareFocus[3 * i + 2] = uint32_t(uintptr_t(lists[i] + 12));
    }

    st.Reset(2);
    CHECK_EQ(st.page, 2u);
    CHECK_EQ(st.offset, 0u);
    uint32_t page[0x240 / 4] = {};

    rs7menu::SwapFocusLists(uint32_t(uintptr_t(page)), st.spareFocus);

    for (uint32_t i = 0; i < 2; ++i) {
        CHECK_EQ(page[rs7menu::kFocusLists / 4 + 3 * i], uint32_t(uintptr_t(lists[i])));
        CHECK_EQ(page[rs7menu::kFocusLists / 4 + 3 * i + 1], uint32_t(uintptr_t(lists[i] + 6)));
        CHECK_EQ(page[rs7menu::kFocusLists / 4 + 3 * i + 2], uint32_t(uintptr_t(lists[i] + 12)));
    }

    rs7menu::SwapFocusLists(uint32_t(uintptr_t(page)), st.spareFocus);
    st.Reset(3);
    CHECK_EQ(st.spareFocus[0], uint32_t(uintptr_t(lists[0])));
    CHECK_EQ(st.spareFocus[3], uint32_t(uintptr_t(lists[1])));
}

TEST(arrangements_coop_reopened_page_keeps_the_other_players_window) {
    rs7arr::PageStates pages;
    rs7arr::PageState* a = pages.Claim(1);
    rs7arr::PageState* b = pages.Claim(2);

    CHECK(a && b && a != b);
    a->offset = 3;
    b->spareFocus[0] = 123;
    pages.Retire(2);
    CHECK(!pages.Find(2));
    rs7arr::PageState* c = pages.Claim(3);

    CHECK(c == b);
    CHECK(pages.Find(1) == a);
    CHECK_EQ(a->offset, 3u);
    CHECK_EQ(c->offset, 0u);
    CHECK_EQ(c->spareFocus[0], 123u);
    CHECK(!pages.Claim(4));  // never evict either live player
    CHECK(pages.Claim(1) == a);
    pages.Retire(99);  // closing an untracked page changes neither player
    CHECK(pages.Find(1) == a);
    CHECK(pages.Find(3) == c);
}
