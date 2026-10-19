#include "features/tuner/tuner.h"

// The tuner's pure rules (tests/tuner): the wrong-instrument threshold and the native session's string order.

int rs7tuner::WrongThreshold(int lowestOpen) {
    return lowestOpen - 1 < 34 ? lowestOpen - 1 : 34;
}

void rs7tu::Order(int n, uint8_t out[8]) {
    for (int i = 0; i < 8; ++i) {
        out[i] = uint8_t(i);
    }

    if (n <= 6) {
        return;
    }

    int k = 0;

    for (int s = n - 1; s >= 6; --s) {
        out[k++] = uint8_t(s);
    }

    for (int s = 0; s < 6; ++s) {
        out[k++] = uint8_t(s);
    }
}

int rs7tu::Step(const uint8_t* order, int n, int cur, int dir) {
    if (n < 1 || n > 8) {
        return -1;
    }

    for (int r = 0; r < n; ++r) {
        if (order[r] == cur) {
            const int to = r + dir;

            return to >= 0 && to < n ? order[to] : -1;
        }
    }

    return -1;
}

void rs7tu::Targets(const int* offs, const int extra[2], int open[8], int off[8]) {
    for (int s = 0; s < 8; ++s) {
        off[s] = s < 6 ? offs[s] : extra[s - 6];
        open[s] = kGuitarBase[s] + off[s];
    }
}

const char* rs7tu::NoteName(int midi) {
    static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    return kNames[((midi % 12) + 12) % 12];
}
