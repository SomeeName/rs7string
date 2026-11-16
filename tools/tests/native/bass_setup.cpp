#include "bass_setup.inc"
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>

static std::shared_ptr<const rs7ext::Block> block;
static bool lowB = false, publishedAtLog = true;
static unsigned logs = 0;

std::shared_ptr<const rs7ext::Block> Rs7Ext_Find(const void*) {
    return block;
}

void rs7colour::SetBassLowB(bool on) {
    lowB = on;
}

void Rs7Log(const char*, ...) {
    ++logs;
    publishedAtLog = publishedAtLog && g_rs7_bass_strings == (block ? 6u : 0u);
    throw std::bad_alloc();
}

int main() {
    std::set_terminate([] { std::_Exit(10); });
    auto bass = std::make_shared<rs7ext::Block>();

    bass->instrument = rs7ext::kInstrumentBass;
    bass->stringCount = 6;
    block = bass;
    uint8_t song[0x200] = {};
    int det = 0;
    if (Rs7Bass_OnSong(&det, song + 0x110, 2) != 6 || Rs7Bass_DetectorStrings(&det) != 6 || !lowB || logs != 1 ||
        !publishedAtLog) {
        return 1;
    }

    block.reset();
    if (Rs7Bass_OnSong(&det, song + 0x110, 2) != 0 || Rs7Bass_DetectorStrings(&det) != 0 || lowB || logs != 2 ||
        !publishedAtLog) {
        return 2;
    }

    std::puts("PASS: diagnostic allocation failure leaves bass setup published");
}
