#pragma once
#include "core/chordloops.h"
#include <cstring>
#include <initializer_list>

extern "C" {
#define CL_DECL(id)                                                                                                    \
    void Rs7_Stub_##id();                                                                                              \
    extern uintptr_t g_rs7_resume_##id;
CL_DECL(CLBM) CL_DECL(CLBA) CL_DECL(CLGU) CL_DECL(CLGE) CL_DECL(CLVB)
#undef CL_DECL
}

inline bool Rs7ChordLoops_Replaced(const char* id) {
    for (const char* old : {"N22", "N23", "NF2", "NF3", "CT4", "CT5", "CT6", "CT7", "CT8", "CT9", "CT10", "CT11"}) {
        if (!std::strcmp(id, old)) {
            return true;
        }
    }

    return false;
}
