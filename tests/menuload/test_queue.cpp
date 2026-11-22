#include "features/menuload/queue.h"
#include "test.h"
#include <string>

using namespace rsxrmenu;

namespace {
const char kTitle[] = "urn:application:x-shockwave-flash:titlescreen";

Request* Next(Queue& q, uint64_t now = 0) {
    Why why{};
    auto* r = q.NextReplay(now, why);

    CHECK(!r || why == Why::Published);

    return r;
}
}  // namespace

TEST(menu_queue_accepted_request_is_never_replayed) {
    Queue q;

    CHECK(q.Ask(0x1000, 7, kTitle));
    CHECK_EQ(q.active.load(), 1L);
    CHECK(q.Result(0x1000, true, 0));
    CHECK_EQ(q.active.load(), 0L);
    CHECK(!q.Publish("titlescreen.gfx"));
    CHECK(!Next(q));
}

TEST(menu_queue_rejected_request_replays_once_per_matching_publication) {
    Queue q;

    q.Ask(0x1000, 7, kTitle);
    auto* r = q.Result(0x1000, false, 0);

    CHECK(r);
    CHECK(r->stage.load() == Stage::Rejected);
    CHECK_EQ(std::string(r->name), std::string("titlescreen"));
    CHECK(!Next(q));
    CHECK(!q.Publish("mainmenu.gfx"));
    CHECK(!q.Publish("titlescreen_old.gfx"));
    CHECK(!Next(q));
    CHECK(q.Publish("data\\gfxassets\\views\\TitleScreen.GFX"));
    CHECK(Next(q) == r);
    CHECK(!Next(q));

    q.Result(0x1000, false, 0);  // still not resolvable: wait for the next publication, no loop
    CHECK(!Next(q));
    q.Publish("titlescreen.gfx");
    CHECK(Next(q) == r);
    q.Result(0x1000, true, 0);
    CHECK_EQ(q.active.load(), 0L);
}

TEST(menu_queue_publication_during_the_request_is_not_lost) {
    Queue q;

    q.Ask(0x1000, 7, kTitle);
    CHECK(q.Publish("titlescreen.gfx"));  // between the failed lookup and its result
    auto* r = q.Result(0x1000, false, 0);

    CHECK(Next(q) == r);
}

TEST(menu_queue_ignores_untracked_movies_and_bad_uris) {
    Queue q;

    CHECK(!q.Result(0x2000, false, 0));
    CHECK(!q.Ask(0, 1, kTitle));
    CHECK(!q.Ask(0x1000, 1, nullptr));
    CHECK(!q.Ask(0x1000, 1, "urn:x:"));
    CHECK(!q.Ask(0x1000, 1, "urn:x:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    CHECK_EQ(q.active.load(), 0L);
}

TEST(menu_queue_reask_reuses_the_slot_and_full_queue_recycles) {
    Queue q;

    auto* a = q.Ask(0x1000, 1, kTitle);

    CHECK(q.Ask(0x1000, 2, kTitle) == a);
    CHECK_EQ(q.active.load(), 1L);

    for (uint32_t i = 1; i < 16; ++i) {
        CHECK(q.Ask(0x1000 + i * 0x100, i, kTitle));
    }

    CHECK_EQ(q.active.load(), 16L);
    CHECK(q.Ask(0x9000, 99, "urn:x:notifications"));
    CHECK_EQ(q.active.load(), 16L);
}

TEST(menu_queue_name_matching) {
    CHECK(NameMatchesFile("titlescreen", "titlescreen.gfx"));
    CHECK(NameMatchesFile("TitleScreen", "a/b\\titlescreen.gfx"));
    CHECK(!NameMatchesFile("titlescreen", "titlescreen"));
    CHECK(!NameMatchesFile("titlescreen", "title.gfx"));
    CHECK(!NameMatchesFile("", "x.gfx"));
    CHECK(!NameMatchesFile("x", nullptr));
}

TEST(menu_queue_falls_back_only_when_no_publication_comes) {
    Queue q;
    Why why{};

    q.Ask(0x1000, 7, kTitle);
    auto* r = q.Result(0x1000, false, 1000);

    CHECK(!q.NextReplay(1000 + kFallbackMs - 1, why));
    CHECK(q.NextReplay(1000 + kFallbackMs, why) == r);
    CHECK(why == Why::Fallback);
    CHECK_EQ(r->fallbacks, 1u);
    CHECK(!q.NextReplay(1000 + kFallbackMs, why));  // the wait restarts

    q.Publish("titlescreen.gfx");  // a publication still wins at once
    CHECK(q.NextReplay(1000 + kFallbackMs + 1, why) == r);
    CHECK(why == Why::Published);

    uint64_t now = 1000 + kFallbackMs;

    for (unsigned i = 1; i < kFallbackMax; ++i) {
        now += kFallbackMs;
        CHECK(q.NextReplay(now, why) == r);
    }

    CHECK_EQ(r->fallbacks, kFallbackMax);
    CHECK(!q.NextReplay(now + 100 * kFallbackMs, why));  // gives up; never a tight loop
}

TEST(menu_queue_future_timestamp_never_triggers_a_fallback) {
    Queue q;
    Why why{};

    q.Ask(0x1000, 7, kTitle);
    q.Result(0x1000, false, 10000);  // stamped by a re-entered result hook after the caller read its clock
    CHECK(!q.NextReplay(9999, why));
    CHECK(!q.NextReplay(10000 + kFallbackMs - 1, why));
    CHECK(q.NextReplay(10000 + kFallbackMs, why));
}
