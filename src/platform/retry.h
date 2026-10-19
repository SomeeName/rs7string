#pragma once

// Startup work that waits for the game, e.g. for VMProtect to finish unpacking .text: calls attempt() until it
// returns true, at most `tries` times, and sleeps pauseMs after every false (also after the last one). Returns
// the number of calls.
void Rs7_SleepMs(unsigned ms);

template <class Attempt>
int Rs7_Retry(int tries, unsigned pauseMs, Attempt attempt) {
    int calls = 0;

    while (calls < tries) {
        ++calls;
        if (attempt()) {
            break;
        }

        Rs7_SleepMs(pauseMs);
    }

    return calls;
}
