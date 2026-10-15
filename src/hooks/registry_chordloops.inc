// Rs7Hook entries of the [chords] native_chordloops group, included into the hook table in registry.cpp.
// {id, stub, resume, target, diagOnly, ref}: the five jmp sites (CLBM CLBA CLGU CLGE CLVB) have their
// stubs in stubs_chordloops.cpp; the others have a null stub, they are in-place byte patches (the table
// row only registers the site; the bytes come from sites.json). All of them are written together.
    {"CLB0", nullptr, nullptr, nullptr, false, false},
    {"CLBM", reinterpret_cast<void*>(&Rs7_Stub_CLBM), &g_rs7_resume_CLBM, nullptr, false, false},
    {"CLBF", nullptr, nullptr, nullptr, false, false},
    {"CLB1", nullptr, nullptr, nullptr, false, false},
    {"CLBG", nullptr, nullptr, nullptr, false, false},
    {"CLBA", reinterpret_cast<void*>(&Rs7_Stub_CLBA), &g_rs7_resume_CLBA, nullptr, false, false},
    {"CLGU", reinterpret_cast<void*>(&Rs7_Stub_CLGU), &g_rs7_resume_CLGU, nullptr, false, false},
    {"CLGE", reinterpret_cast<void*>(&Rs7_Stub_CLGE), &g_rs7_resume_CLGE, nullptr, false, false},
    {"CLVB", reinterpret_cast<void*>(&Rs7_Stub_CLVB), &g_rs7_resume_CLVB, nullptr, false, false},
    {"CLNF2", nullptr, nullptr, nullptr, false, false},
    {"CLNF3", nullptr, nullptr, nullptr, false, false},
    {"CLCT4", nullptr, nullptr, nullptr, false, false},
    {"CLCT5", nullptr, nullptr, nullptr, false, false},
    {"CLCT6", nullptr, nullptr, nullptr, false, false},
    {"CLCT7", nullptr, nullptr, nullptr, false, false},
    {"CLCT8", nullptr, nullptr, nullptr, false, false},
    {"CLCT9", nullptr, nullptr, nullptr, false, false},
    {"CLCT10", nullptr, nullptr, nullptr, false, false},
    {"CLCT11", nullptr, nullptr, nullptr, false, false},
