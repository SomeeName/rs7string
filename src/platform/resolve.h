#pragma once
#include "platform/sites.h"
#include <cstdint>
#include <string>
#include <vector>

struct Rs7Resolved {
    const Rs7Site* site;
    uintptr_t addr;                 // site address
    uintptr_t target;               // original branch/call target, 0 if none
    std::vector<uint8_t> original;  // the `expect` bytes, for restoring
};

struct Rs7ResolveResult {
    bool ok;  // true only if every site resolved
    std::vector<Rs7Resolved> sites;
    std::vector<std::string> errors;     // "<id>: <reason>"
    std::vector<Rs7Resolved> resolved;   // sites that did resolve, kept even when ok is false (per-feature isolation)
    std::vector<const Rs7Site*> failed;  // sites that did not, parallel to errors
};

// Resolves every site in [text, text + size); textVa is the runtime address of text[0].
Rs7ResolveResult Rs7_ResolveAll(const Rs7Site* sites, int count, const uint8_t* text, size_t size, uintptr_t textVa);

// Whether another resolve attempt can still help. Retrying waits for VMProtect to unpack .text, when most sites fail.
// Once at most 1 in 20 sites fails and an attempt fails exactly the sites the previous one failed (`previous`), the
// failures are not unpacking (a site another mod hooked first, e.g. RSMods): retrying only delays the patch past the
// game's start-up (its ColourManager), so the caller stops and isolates them.
bool Rs7_ResolveSettled(const Rs7ResolveResult& res, const std::vector<const Rs7Site*>& previous, int count);
