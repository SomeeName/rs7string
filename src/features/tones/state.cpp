#include "features/tones/state.h"
#include "features/tones/game.h"
#include "features/tones/native_registration.h"
#include "features/tones/toneplan.h"
#include "features/tones/tones.h"
#include "model/gamestring.h"
#include "model/manifest.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include <cstring>
#include <map>
#include <mutex>

extern "C" uintptr_t g_rsxr_tone_manager;

namespace rsxrtones {
namespace {
struct Playback {
    Player selection;
    uint32_t song = 0, session = 0;
    uint64_t generation = 0;
    Identity id{};
    std::shared_ptr<const Arrangement> arrangement;
    const char* inactive = nullptr;
    uint32_t rejected = kInvalid;
    bool rejection = false, firstEvent = false;
    bool registrationAttempted = false;
    std::shared_ptr<NativeRegistration> registration;
    std::shared_ptr<Tone> custom;
    uint32_t rejectedObject = 0;
};

struct Song {
    uint64_t generation;
    bool native;
};

struct State {
    std::mutex mutex;
    std::map<Identity, std::shared_ptr<const Arrangement>> arrangements;
    std::map<uint32_t, Song> songs;
    uint64_t generation = 0;
    Playback players[2];  // accessed only by the game thread; loaders touch only the two containers above
};

State g_state;

bool IdentityAt(uint32_t address, Identity& id) {
    return address && Rsxr_SafeCopy(id.data(), reinterpret_cast<const void*>(uintptr_t(address)), sizeof id);
}

std::shared_ptr<const Arrangement> FindArrangement(uint32_t session, uint32_t song, uint64_t& generation, Identity& id,
                                                   const char*& inactive) {
    if (!IdentityAt(session + 0x68, id)) {
        inactive = "rejected: unreadable arrangement identity";
        return {};
    }

    std::lock_guard lock(g_state.mutex);
    const auto it = g_state.arrangements.find(id);
    const auto loaded = g_state.songs.find(song);

    if (loaded == g_state.songs.end()) {
        inactive = "rejected: SNG load not observed";
    } else {
        generation = loaded->second.generation;
        inactive = loaded->second.native ? nullptr : "rejected: SNG is not native";
    }

    if (!g_rsxr_tones) {
        inactive = "rejected: feature disabled";
    }

    const auto arrangement = it == g_state.arrangements.end() ? nullptr : it->second;

    if (!inactive) {
        inactive = arrangement ? arrangement->inactive : "rejected: manifest not captured";
    }

    return arrangement;
}

void Replace(const Identity& id, std::shared_ptr<const Arrangement> arrangement) {
    // Release replaced game objects outside the mutex: game vtables may enter other hooks.
    std::shared_ptr<const Arrangement> old;

    {
        std::lock_guard lock(g_state.mutex);
        const auto it = g_state.arrangements.find(id);

        if (it != g_state.arrangements.end()) {
            old = std::move(it->second);
            g_state.arrangements.erase(it);
        }

        if (arrangement) {
            g_state.arrangements.emplace(id, std::move(arrangement));
        }
    }
}

void Log(const Arrangement& a, uint32_t player, uint32_t id, const char* key, bool applied) {
    RsxrLog("tones26: arrangement=%08X%08X%08X%08X player=%u id=%u key='%s' %s", a.id[0], a.id[1], a.id[2], a.id[3],
            player, id, key, applied ? "applied" : "rejected");
}

void LogDecision(const Playback& p, uint32_t player) {
    const auto& id = p.id;

    if (p.inactive) {
        RsxrLog("tones26: arrangement=%08X%08X%08X%08X song=%08X player=%u %s", id[0], id[1], id[2], id[3], p.song,
                player, p.inactive);
    } else {
        const auto& slots = p.arrangement->slots;

        RsxrLog("tones26: arrangement=%08X%08X%08X%08X song=%08X player=%u extra tones %zu (%s), active", id[0], id[1],
                id[2], id[3], p.song, player, (slots.size() + 1) / 2, slots.c_str());
    }
}

Playback& Observe(uint32_t session, uint32_t song, uint32_t player, bool event, uint32_t raw) {
    uint64_t generation = 0;
    Identity id{};
    const char* inactive = nullptr;
    const auto arrangement = FindArrangement(session, song, generation, id, inactive);
    Playback& p = g_state.players[player];
    const bool load = p.song != song || p.generation != generation || p.session != session || p.id != id;

    if (load || p.arrangement != arrangement || p.inactive != inactive) {
        const bool firstEvent = !load && p.firstEvent;

        p = {};
        p.song = song;
        p.session = session;
        p.generation = generation;
        p.id = id;
        p.arrangement = arrangement;
        p.inactive = inactive;
        p.firstEvent = firstEvent;
        LogDecision(p, player);
    }

    if (event && !p.firstEvent) {
        const float time = Rsxr_SafeRead<float>(reinterpret_cast<const void*>(uintptr_t(session)), 0x3B4);

        p.firstEvent = true;
        RsxrLog("tones26: arrangement=%08X%08X%08X%08X song=%08X player=%u first event raw=%u time=%.3f", id[0], id[1],
                id[2], id[3], song, player, raw, static_cast<double>(time));
    }

    return p;
}

uint32_t Object(const Arrangement& a, uint32_t session, uint32_t id, char (&key)[256]) {
    if (id >= 4 && id < kSlots) {
        const Tone& tone = a.extra[id - 4];

        strcpy_s(key, tone.key.c_str());

        return tone.object;
    }

    const uint32_t index = id == kBase ? 0 : id + 1;

    (void)rsxr::ReadGameString(reinterpret_cast<const void*>(uintptr_t(session + 0x198 + 0x18 * index)), key,
                               sizeof key);

    return Read(session, 0x228 + 4 * index);
}

void LogOverride(uint32_t id, const char* key, bool applied) {
    const uint32_t slot = id == kBase ? 0 : id + 1;
    const char* extension = SlotExtension(slot);

    if (extension) {
        RsxrLog("tones26: override slot=%s key='%s' %s", extension + 4, key, applied ? "applied" : "none");
    }
}

void RegisterExtras(Playback& playback, uint32_t player) {
    if (playback.inactive || playback.registrationAttempted) {
        return;
    }

    playback.registrationAttempted = true;
    const auto arrangement = playback.arrangement;
    std::shared_ptr<NativeRegistration> registration;

    try {
        registration = std::make_shared<NativeRegistration>();
    } catch (...) {
        // Still log one rejected decision per declared slot below.
    }

    playback.registration = registration;
    const bool manager = Read(static_cast<uint32_t>(g_rsxr_tone_manager)) != 0;

    for (uint32_t id = 4; id < kSlots; ++id) {
        const Tone& tone = arrangement->extra[id - 4];
        if (tone.key.empty()) {
            continue;
        }

        const bool ok = manager && registration && registration->Register(tone.object, player, id + 1);
        const char* reason = !tone.object    ? "rejected: missing or ambiguous definition"
                             : !manager      ? "rejected: tone manager unavailable"
                             : !registration ? "rejected: resource store allocation failed"
                                             : "rejected: registration unavailable or failed";

        RsxrLog("tones26: register player=%u slot=%u key='%s' %s", player, id + 1, tone.key.c_str(),
                ok ? "ok" : reason);
    }
}
}  // namespace

Tone::~Tone() {
    if (object) {
        (void)Reference(object, false);
    }
}

void Capture(uint32_t attributes, uint32_t collection, const Identity& id) {
    double marker = 0;

    if (!attributes) {
        Replace(id, {});
        return;
    }

    auto arrangement = std::make_shared<Arrangement>();
    const bool eligible =
        rsxr::JsonNumber(reinterpret_cast<void*>(uintptr_t(attributes)), "RsxrToneSlots", &marker) && marker == 26;

    arrangement->id = id;

    for (uint32_t i = 4; i < kSlots; ++i) {
        char field[] = "Tone_E";
        char key[256]{};

        field[5] = static_cast<char>('A' + i);

        if (!Name(attributes, field, key, sizeof key) || !key[0]) {
            continue;
        }

        Tone& tone = arrangement->extra[i - 4];

        tone.key = key;

        if (!arrangement->slots.empty()) {
            arrangement->slots += ',';
        }

        arrangement->slots += field[5];

        if (g_rsxr_tones && eligible) {
            tone.object = Definition(collection, key);
        }
    }

    if (!arrangement->slots.empty()) {
        arrangement->inactive = eligible ? nullptr : "rejected: missing or invalid RsxrToneSlots";
    } else if (eligible) {
        arrangement->inactive = "rejected: marked manifest has no readable extra tones";
    }

    RsxrLog("tones26: manifest arrangement=%08X%08X%08X%08X extra tones=%zu slots=%s marker=%.0f %s", id[0], id[1],
            id[2], id[3], (arrangement->slots.size() + 1) / 2, arrangement->slots.c_str(), marker,
            arrangement->inactive ? arrangement->inactive : "captured");
    Replace(id, std::move(arrangement));
}

bool Handle(uint32_t session, bool event, uint32_t raw, bool force) {
    if (!session) {
        return false;
    }

    const uint32_t player = Read(session, 8), song = Read(session, 0x78);
    if (player >= 2) {
        return false;
    }

    Playback& playback = Observe(session, song, player, event, raw);
    if (playback.inactive) {
        return false;
    }

    RegisterExtras(playback, player);
    const auto& arrangement = playback.arrangement;
    const uint32_t id = Select(event, raw), rejected = event ? raw : kBase;
    Tone custom;

    const bool declared = id < 4 || id == kBase || (id < kSlots && !arrangement->extra[id - 4].key.empty());

    if (declared) {
        custom.object = Override(player, id == kBase ? 0 : id + 1);
    }

    const bool overrideChanged = custom.object != (playback.custom ? playback.custom->object : 0);

    if (!force && !playback.rejection && !overrideChanged && !playback.selection.Needs(session, id)) {
        return true;
    }

    char key[256]{};
    const uint32_t object = custom.object ? custom.object : id == kInvalid ? 0 : Object(*arrangement, session, id, key);

    if (custom.object) {
        (void)Name(custom.object, "Key", key, sizeof key);
    }

    const bool changed = playback.selection.Needs(session, id) || playback.rejection || overrideChanged;
    const bool applied = object && Apply(object, player);

    if (applied) {
        if (overrideChanged) {
            auto retained = custom.object ? std::make_shared<Tone>() : nullptr;

            if (retained) {
                retained->object = custom.object;
                custom.object = 0;
            }

            playback.custom = std::move(retained);
        }

        playback.selection.Applied(session, id);
        playback.rejection = false;

        if (changed) {
            Log(*arrangement, player, id, key, true);
            LogOverride(id, key, playback.custom != nullptr);
        }
    } else if (!playback.rejection || playback.rejected != rejected || playback.rejectedObject != object) {
        playback.rejection = true;
        playback.rejected = rejected;
        playback.rejectedObject = object;
        Log(*arrangement, player, rejected, key, false);
        LogOverride(id, key, false);
    }

    return true;
}

void PreRegister(uint32_t session) {
    if (!session) {
        return;
    }

    const uint32_t player = Read(session, 8);
    if (player >= 2) {
        return;
    }

    Playback& playback = Observe(session, Read(session, 0x78), player, false, 0);

    RegisterExtras(playback, player);
}
}  // namespace rsxrtones

void RsxrTones_SongLoaded(uint32_t song, bool native) {
    if (!song) {
        return;
    }

    try {
        uint64_t generation = 0;

        {
            std::lock_guard lock(rsxrtones::g_state.mutex);

            generation = ++rsxrtones::g_state.generation;
            rsxrtones::g_state.songs.insert_or_assign(song, rsxrtones::Song{generation, native});
        }

        RsxrLog("tones26: song=%08X load=%llu %s", song, static_cast<unsigned long long>(generation),
                native ? "native SNG accepted" : "stock or rejected SNG; extended tones inactive");
    } catch (...) {
        RsxrLog("tones26: SNG marker allocation failed; extended tones unavailable for song=%08X", song);
    }
}
