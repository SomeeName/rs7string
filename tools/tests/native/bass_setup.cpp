#include "bass_setup.inc"
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>

static std::shared_ptr<const rsxrext::Block> block;
static bool lowB = false, publishedAtLog = true;
static unsigned logs = 0;

std::shared_ptr<const rsxrext::Block> RsxrExt_Find(const void*) {
    return block;
}

void rsxrcolour::SetBassLowB(bool on) {
    lowB = on;
}

void RsxrLog(const char*, ...) {
    ++logs;
    publishedAtLog = publishedAtLog && g_rsxr_bass_strings == (block ? 6u : 0u);
    throw std::bad_alloc();
}

int main() {
    std::set_terminate([] { std::_Exit(10); });
    auto bass = std::make_shared<rsxrext::Block>();

    bass->instrument = rsxrext::kInstrumentBass;
    bass->stringCount = 6;
    block = bass;
    uint8_t song[0x200] = {};
    int det = 0;
    if (RsxrBass_OnSong(&det, song + 0x110, 2) != 6 || RsxrBass_DetectorStrings(&det) != 6 || !lowB || logs != 1 ||
        !publishedAtLog) {
        return 1;
    }

    block.reset();
    if (RsxrBass_OnSong(&det, song + 0x110, 2) != 0 || RsxrBass_DetectorStrings(&det) != 0 || lowB || logs != 2 ||
        !publishedAtLog) {
        return 2;
    }

    std::puts("PASS: diagnostic allocation failure leaves bass setup published");
}
