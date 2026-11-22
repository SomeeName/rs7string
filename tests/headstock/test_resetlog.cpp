#include "features/headstock/headstock.h"
#include "features/headstock/state.h"
#include "test.h"

// RsxrHead_ResetLog (song setup) lets every once-per-song headstock log line appear again, the "own model full" line
// included (it was not reset before).
TEST(headstock_reset_log_clears_every_log_once_flag) {
    rsxrhead::State& h = rsxrhead::g_head;
    volatile long* flags[] = {&h.logOwnFull, &h.logCtor, &h.logLoad, &h.logRaise, &h.logNoSlot, &h.logForeign};
    long saved[6];

    for (int i = 0; i < 6; ++i) {
        saved[i] = *flags[i];
        *flags[i] = 1;
    }

    RsxrHead_ResetLog();

    for (int i = 0; i < 6; ++i) {
        CHECK_EQ(*flags[i], 0L);
        *flags[i] = saved[i];
    }
}
