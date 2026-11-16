// A busy-wait lock for very short critical sections that two threads share (game, render, overlay). Not reentrant.
#pragma once
#include <intrin.h>

inline void Rs7Spin_Lock(volatile long* spin) {
    while (_InterlockedCompareExchange(spin, 1, 0) != 0) {
        _mm_pause();
    }
}

inline void Rs7Spin_Unlock(volatile long* spin) {
    _InterlockedExchange(spin, 0);
}
