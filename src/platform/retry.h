#pragma once

// Sleep(ms), kept out of this header so it needs no <windows.h>.
void Rsxr_SleepMs(unsigned ms);

// Startup work that waits for the game, e.g. for VMProtect to finish unpacking .text: calls attempt() until it
// returns true, at most `tries` times, and sleeps pauseMs after every false (also after the last one). Returns
// the number of calls.
template <class Attempt>
int Rsxr_Retry(int tries, unsigned pauseMs, Attempt attempt) {
    int calls = 0;

    while (calls < tries) {
        ++calls;
        if (attempt()) {
            break;
        }

        Rsxr_SleepMs(pauseMs);
    }

    return calls;
}
