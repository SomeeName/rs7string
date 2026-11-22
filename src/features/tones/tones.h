#pragma once
#include "platform/hooktable.h"

extern const RsxrHookTable kRsxrToneHooks;
extern uintptr_t g_rsxr_tones;

// Called at XI1 for every SNG load. Each load gets a generation, including stock loads at reused addresses.
void RsxrTones_SongLoaded(uint32_t song, bool native);
