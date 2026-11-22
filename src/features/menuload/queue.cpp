#include "features/menuload/queue.h"
#include <cctype>
#include <cstring>

namespace {
char Lower(char c) {
    return char(std::tolower(static_cast<unsigned char>(c)));
}
}  // namespace

bool rsxrmenu::NameMatchesFile(const char* name, const char* file) {
    if (!name || !name[0] || !file) {
        return false;
    }

    for (const char* p = file; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            file = p + 1;
        }
    }

    size_t i = 0;

    for (; name[i]; ++i) {
        if (Lower(name[i]) != Lower(file[i])) {
            return false;
        }
    }

    return file[i] == '.';
}

rsxrmenu::Request* rsxrmenu::Queue::Ask(uint32_t movie, uint64_t owner, const char* uri) {
    const char* name = uri ? std::strrchr(uri, ':') : nullptr;

    name = name ? name + 1 : uri;

    if (!movie || !name || !name[0] || std::strlen(name) >= sizeof items[0].name) {
        return nullptr;
    }

    Request* slot = nullptr;

    for (auto& r : items) {
        if (r.stage.load() != Stage::Free && r.movie == movie) {
            slot = &r;
            break;
        }
    }

    for (size_t i = 0; !slot && i < sizeof items / sizeof items[0]; ++i) {
        if (items[i].stage.load() == Stage::Free) {
            slot = &items[i];
        }
    }

    if (!slot) {  // all busy: the oldest slot is most likely a dead movie
        slot = &items[next++ % (sizeof items / sizeof items[0])];
    }

    // Exclusive: no indexing worker is reading this slot's name or flagging it while it is reused.
    const std::unique_lock lock(reuse);

    if (slot->stage.load() != Stage::Free) {
        Drop(*slot);
    }

    slot->published = 0;
    slot->movie = movie;
    slot->owner = owner;
    slot->waitingSince = 0;
    slot->replays = slot->fallbacks = 0;
    std::memcpy(slot->name, name, std::strlen(name) + 1);
    slot->stage.store(Stage::Asked);
    ++active;

    return slot;
}

rsxrmenu::Request* rsxrmenu::Queue::Result(uint32_t movie, bool accepted, uint64_t now) {
    for (auto& r : items) {
        if (r.stage.load() == Stage::Free || r.movie != movie) {
            continue;
        }

        if (accepted) {
            Drop(r);
        } else {
            r.waitingSince = now;
            r.stage.store(Stage::Rejected);
        }

        return &r;
    }

    return nullptr;
}

bool rsxrmenu::Queue::Publish(const char* file) {
    if (!active.load()) {
        return false;
    }

    bool hit = false;
    const std::shared_lock lock(reuse);

    for (auto& r : items) {
        if (r.stage.load() != Stage::Free && NameMatchesFile(r.name, file)) {
            r.published = 1;
            hit = true;
        }
    }

    return hit;
}

rsxrmenu::Request* rsxrmenu::Queue::NextReplay(uint64_t now, Why& why) {
    if (!active.load()) {
        return nullptr;
    }

    for (auto& r : items) {
        if (r.stage.load() == Stage::Rejected && r.published.exchange(0)) {
            why = Why::Published;
            return &r;
        }
    }

    for (auto& r : items) {
        if (r.stage.load() == Stage::Rejected && r.fallbacks < kFallbackMax && now >= r.waitingSince &&
            now - r.waitingSince >= kFallbackMs) {
            ++r.fallbacks;
            r.waitingSince = now;
            why = Why::Fallback;
            return &r;
        }
    }

    return nullptr;
}

void rsxrmenu::Queue::Drop(Request& r) {
    if (r.stage.exchange(Stage::Free) != Stage::Free) {
        --active;
    }
}
