#pragma once
#include <atomic>
#include <cstdint>
#include <shared_mutex>

// Menu movie requests the game's asset locator rejected because the file was not indexed yet. The game drops such a
// request and never asks again (the stock "white loading screen" soft lock). Each rejected request is sent once more
// when the indexer publishes the matching file. Only if that event never comes (an unknown loss path) is a rejected
// request sent again after kFallbackMs, at most kFallbackMax times, with a warning.
//
// Threads: Ask, Result, NextReplay and Drop run on the game thread. Publish runs on the indexing workers and only
// reads names and sets the published flag, under a shared lock that Ask takes exclusively before it reuses a slot.
namespace rs7menu {
enum class Stage : uint8_t { Free, Asked, Rejected };

constexpr uint64_t kFallbackMs = 5000;
constexpr unsigned kFallbackMax = 6;
enum class Why : uint8_t { Published, Fallback };

struct Request {
    std::atomic<Stage> stage{Stage::Free};
    std::atomic<long> published{0};
    uint32_t movie = 0;
    uint64_t owner = 0;
    uint64_t waitingSince = 0;
    unsigned replays = 0, fallbacks = 0;
    char name[64] = {};
};

struct Queue {
    Request items[16];
    std::atomic<long> active{0};
    std::shared_mutex reuse;
    uint32_t next = 0;

    // Before the game's request: name = the URI's last ':' segment ("urn:...:titlescreen" -> "titlescreen").
    Request* Ask(uint32_t movie, uint64_t owner, const char* uri);
    // After the game's request; accepted ends tracking. Returns the entry, or null for an untracked movie.
    Request* Result(uint32_t movie, bool accepted, uint64_t now);
    // An indexed file ("titlescreen.gfx", any folder, any case). Returns true if a tracked request matches.
    bool Publish(const char* file);
    // A rejected request whose file has been published since (clears the flag), or failing that, one that has waited
    // kFallbackMs for it (restarts the wait). A timestamp later than now never counts as waited.
    Request* NextReplay(uint64_t now, Why& why);
    void Drop(Request& r);
};

bool NameMatchesFile(const char* name, const char* file);
}  // namespace rs7menu
