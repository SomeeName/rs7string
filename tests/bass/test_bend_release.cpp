#include "features/bass/bass.h"
#include "test.h"

TEST(bass_bend_release_requires_continuous_low_readings) {
    for (const bool ok : {false, true}) {
        rsxrbass::BendHold h;
        int note = 0;
        float v = 2.0f;

        CHECK(rsxrbass::HoldBend(h, &note, true, &v, 1.0f));
        v = 0.0f;
        CHECK(rsxrbass::HoldBend(h, &note, true, &v, 1.02f) && v == 2.0f);
        v = -1.0f;
        CHECK(rsxrbass::HoldBend(h, &note, ok, &v, 2.0f) && v == 2.0f);
        v = 0.0f;
        CHECK(rsxrbass::HoldBend(h, &note, true, &v, 2.02f) && v == 2.0f);
        v = 0.0f;
        CHECK(rsxrbass::HoldBend(h, &note, true, &v, 2.10f) && v == 2.0f);
        v = 0.0f;
        CHECK(rsxrbass::HoldBend(h, &note, true, &v, 2.20f) && v == 0.0f);
    }
}
