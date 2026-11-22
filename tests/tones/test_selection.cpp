#include "features/tones/selection.h"
#include "test.h"

TEST(tone_slots_include_f_without_using_the_stock_sentinel) {
    CHECK(rsxrtones::Select(true, 4) == 4);
    CHECK(rsxrtones::Select(true, 5) == 5);
    CHECK(rsxrtones::Select(true, 25) == 25);
    CHECK(rsxrtones::Select(false, 5) == rsxrtones::kBase);
    CHECK(rsxrtones::Select(true, 26) == rsxrtones::kInvalid);
    CHECK(rsxrtones::Select(true, 0xFFFFFFFFu) == rsxrtones::kInvalid);
}

TEST(tone_players_and_arrangements_have_independent_selection) {
    rsxrtones::Player players[2];

    CHECK(players[0].Needs(100, 5));
    players[0].Applied(100, 5);
    CHECK(!players[0].Needs(100, 5));
    CHECK(players[1].Needs(100, 5));
    CHECK(players[0].Needs(101, 5));
    CHECK(players[0].Needs(100, rsxrtones::kBase));
    CHECK(players[0].Needs(100, 0));
}
