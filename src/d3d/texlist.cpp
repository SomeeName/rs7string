#include "d3d/texlist.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr size_t kPathMax = sizeof(Rs7TexFound::path);  // 120: up to 119 chars + NUL
constexpr size_t kSeenSlots = 2 * rs7tex::kMaxNodes;    // open addressing, load <= 1/2 (power of two)
constexpr size_t kWhyMax = 64;

bool U32(const Rs7MemReader& m, uintptr_t a, uint32_t* v) {
    return m.Read(a, v, sizeof *v);
}

// Reads a NUL-terminated string of at most kPathMax-1 chars at the pointer stored at `ptrAddr`.
// False if the pointer is null/unreadable, the string unreadable or not terminated in time.
bool Str(const Rs7MemReader& m, uintptr_t ptrAddr, char out[kPathMax]) {
    out[0] = '\0';
    uint32_t p = 0;
    if (!U32(m, ptrAddr, &p) || !p) {
        return false;
    }

    for (size_t i = 0; i < kPathMax; ++i) {
        char c;

        if (!m.Read(uintptr_t(p) + i, &c, 1)) {
            out[0] = '\0';
            return false;
        }

        out[i] = c;
        if (!c) {
            return true;
        }
    }

    out[0] = '\0';

    return false;
}

bool IEquals(const char* a, const char* b) {
    for (;; ++a, ++b) {
        char x = *a, y = *b;

        if (x >= 'A' && x <= 'Z') {
            x = char(x - 'A' + 'a');
        }
        if (y >= 'A' && y <= 'Z') {
            y = char(y - 'A' + 'a');
        }
        if (x != y) {
            return false;
        }
        if (!x) {
            return true;
        }
    }
}

// Validates the renderer data of source texture `obj` (any size). True: rd/d3d/w/h/fmt filled.
// False: why = "<check> (<value>)". No allocation (it runs under the game's list lock).
struct RdInfo {
    uint32_t rd = 0, d3d = 0, w = 0, h = 0, fmt = 0;
};

bool CheckRd(const Rs7MemReader& m, uintptr_t obj, uintptr_t lo, uintptr_t hi, RdInfo* r, char why[kWhyMax]) {
    using namespace rs7tex;
    auto no = [&](const char* check, uint32_t v, bool hex) {
        std::snprintf(why, kWhyMax, hex ? "%s (%08X)" : "%s (%u)", check, v);

        return false;
    };
    uint32_t vt = 0, owner = 0, d3dVt = 0;
    if (!U32(m, obj + kRendData, &r->rd)) {
        return no("unreadable", uint32_t(obj + kRendData), true);
    }
    if (!r->rd) {
        return no("no renderer data", 0, false);
    }
    if (!U32(m, r->rd, &vt) || !U32(m, r->rd + kRdOwner, &owner) || !U32(m, r->rd + kRdW, &r->w) ||
        !U32(m, r->rd + kRdH, &r->h) || !U32(m, r->rd + kRdFmt, &r->fmt) || !U32(m, r->rd + kRdD3d, &r->d3d)) {
        return no("unreadable renderer data", r->rd, true);
    }
    if (vt != kVtRendData) {
        return no("rd vtable", vt, true);
    }
    if (owner != obj) {
        return no("rd owner", owner, true);
    }
    if (!r->d3d) {
        return no("no d3d texture", 0, false);
    }
    if (!U32(m, r->d3d, &d3dVt)) {
        return no("unreadable d3d texture", r->d3d, true);
    }
    if (d3dVt < lo || d3dVt >= hi) {
        return no("d3d vtable", d3dVt, true);
    }

    why[0] = '\0';

    return true;
}

bool IsSource(uint32_t vt) {
    return vt == rs7tex::kVtSource54 || vt == rs7tex::kVtSource50;
}

// True if `node` was already in the table (a cycle); inserts it otherwise. False also when full.
bool SeenBefore(std::vector<uint32_t>& tab, uint32_t node) {
    const size_t mask = tab.size() - 1;

    for (size_t i = (node * 2654435761u) & mask, k = 0; k < tab.size(); i = (i + 1) & mask, ++k) {
        if (tab[i] == node) {
            return true;
        }

        if (tab[i] == 0) {
            tab[i] = node;
            return false;
        }
    }

    return false;  // cannot happen: kMaxNodes stops the walk first
}
}  // namespace

bool Rs7Tex_FamilyOf(const char* name, Rs7Family* f) {
    if (!name) {
        return false;
    }

    const char* base = name;

    for (const char* p = name; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }

    if (IEquals(base, "note_gradient_map.dds")) {
        *f = Rs7Family::Note;
        return true;
    }

    if (IEquals(base, "gradient_map_additive.dds")) {
        *f = Rs7Family::Additive;
        return true;
    }

    return false;
}

void Rs7Tex_Prepare(Rs7TexWalk* w) {
    w->found.reserve(64);
    w->rejects.reserve(64);
    w->seen.assign(kSeenSlots, 0);
}

void Rs7Tex_WalkInto(const Rs7MemReader& m, uintptr_t headAddr, uintptr_t lo, uintptr_t hi, Rs7TexWalk* w) {
    using namespace rs7tex;

    w->nodes = w->sources = w->nameMatches = 0;
    w->cycleOrCap = w->readFault = false;
    w->found.clear();
    w->rejects.clear();
    w->anyD3d = 0;
    if (w->seen.size() != kSeenSlots) {
        w->seen.assign(kSeenSlots, 0);  // not prepared: allocates here
    } else {
        std::fill(w->seen.begin(), w->seen.end(), 0u);
    }
    uint32_t node = 0;

    if (!U32(m, headAddr, &node)) {
        w->readFault = true;
        return;
    }

    while (node) {
        if (w->nodes >= kMaxNodes || SeenBefore(w->seen, node)) {
            w->cycleOrCap = true;
            break;
        }

        uint32_t vt = 0;

        if (!U32(m, node, &vt)) {
            w->readFault = true;
            break;
        }

        ++w->nodes;

        if (IsSource(vt)) {
            ++w->sources;
            char file[kPathMax], plat[kPathMax];
            const bool hasFile = Str(m, node + kFile, file);
            const bool hasPlat = Str(m, node + kPlatFile, plat);
            Rs7Family fam = Rs7Family::Note;
            const bool match = (hasFile && Rs7Tex_FamilyOf(file, &fam)) || (hasPlat && Rs7Tex_FamilyOf(plat, &fam));
            const char* path = hasPlat && plat[0] ? plat : file;
            RdInfo rd;
            char why[kWhyMax];
            const bool valid = CheckRd(m, node, lo, hi, &rd, why);  // device acquisition: any size

            if (valid && !w->anyD3d) {
                w->anyD3d = rd.d3d;
            }

            if (match) {
                ++w->nameMatches;
                if (valid && (rd.w != 256 || rd.h != 128)) {
                    std::snprintf(why, sizeof why, "size (%ux%u)", rd.w, rd.h);
                }

                if (why[0]) {
                    char line[kPathMax + kWhyMax + 32];

                    std::snprintf(line, sizeof line, "obj=%08X '%s': %s", unsigned(node), path, why);
                    w->rejects.emplace_back(line);  // rare: only a ramp name that fails a check allocates
                } else {
                    Rs7TexFound f{};

                    f.obj = node;
                    f.rd = rd.rd;
                    f.d3d = rd.d3d;
                    f.family = fam;
                    f.fmt = rd.fmt;
                    std::memcpy(f.path, path, kPathMax);
                    w->found.push_back(f);  // within the reserved capacity for <= 64 ramps
                }
            }
        }

        uint32_t next = 0;

        if (!U32(m, node + kNext, &next)) {
            w->readFault = true;
            break;
        }

        node = next;
    }
}

Rs7TexWalk Rs7Tex_Walk(const Rs7MemReader& mem, uintptr_t headAddr, uintptr_t d3d9Lo, uintptr_t d3d9Hi) {
    Rs7TexWalk w;

    Rs7Tex_Prepare(&w);
    Rs7Tex_WalkInto(mem, headAddr, d3d9Lo, d3d9Hi, &w);

    return w;
}

bool Rs7Tex_Revalidate(const Rs7MemReader& m, uintptr_t obj, uintptr_t d3d, Rs7Family fam, uintptr_t lo, uintptr_t hi) {
    using namespace rs7tex;
    if (!obj || !d3d) {
        return false;
    }

    uint32_t vt = 0;
    if (!U32(m, obj, &vt) || !IsSource(vt)) {
        return false;
    }

    char file[kPathMax], plat[kPathMax];
    Rs7Family f = Rs7Family::Note;
    const bool fileOk = Str(m, obj + kFile, file) && Rs7Tex_FamilyOf(file, &f) && f == fam;
    const bool platOk = !fileOk && Str(m, obj + kPlatFile, plat) && Rs7Tex_FamilyOf(plat, &f) && f == fam;
    if (!fileOk && !platOk) {
        return false;
    }

    RdInfo rd;
    char why[kWhyMax];

    return CheckRd(m, obj, lo, hi, &rd, why) && rd.d3d == d3d && rd.w == 256 && rd.h == 128;
}
