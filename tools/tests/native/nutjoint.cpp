// Nut joint of the 7th string (fix/nutgap): the extrapolated attach point of the 7th neck string lands on
// the nut end of the edited model's string_size_7, the way the stock attach points of strings 0/1 land on
// the nut ends of string_size_6 / string_size_5 (all spaced 1.608 in z). Numbers: gsnative log line
// "string 6 attach (0.000, -53.653, 5.627)" and the model rs7_inline7 (tools/rs7headstock/build/out,
// geom.npz): string_size_N nut end z = -2.876 -1.268 0.340 1.947 3.555 5.163 6.771 (N = 1..7), y 15.21, in the headstock node at
// T(3.85, -68.85, -1.143) (the measured world end of string_size_7: (0.000, -53.642, 5.628)).
#include <cmath>
#include <cstdio>
#include "core/gsnative.h"

static int fails = 0;
static void Check(bool ok, const char* what, double got, double want) {
    if (!ok) { std::printf("FAIL %s: got %.4f want %.4f\n", what, got, want); ++fails; }
}

int main() {
    const float a0[3] = {0.0f, -53.653f, 4.019f}, a1[3] = {0.0f, -53.653f, 2.411f};
    const double zOff = -1.143, yEnd = -53.642;   // model nut end -> world
    static const double kEndZ[7] = {-2.876, -1.268, 0.340, 1.947, 3.555, 5.163, 6.771};   // model nut end z
    auto segZ = [&](int n) { return kEndZ[n - 1] + zOff; };   // string_size_n nut end, world z
    // Strings 0 and 1 (stock attach) meet string_size_6 and string_size_5.
    Check(std::fabs(a0[2] - segZ(6)) < 0.002, "string 0 attach z vs string_size_6 end", a0[2], segZ(6));
    Check(std::fabs(a1[2] - segZ(5)) < 0.002, "string 1 attach z vs string_size_5 end", a1[2], segZ(5));
    float s6[3], s7[3];
    rs7gs::ExtrapolateAttach(a0, a1, 6, s6);   // the 7th string
    rs7gs::ExtrapolateAttach(a0, a1, 7, s7);   // the 8th
    Check(std::fabs(s6[2] - 5.627) < 0.001, "7th attach z", s6[2], 5.627);
    Check(std::fabs(s6[1] + 53.653) < 1e-4 && std::fabs(s6[0]) < 1e-4, "7th attach x/y", s6[1], -53.653);
    Check(std::fabs(s7[2] - 7.235) < 0.001, "8th attach z", s7[2], 7.235);
    // The 7th meets string_size_7 like strings 0/1 meet theirs: z within 0.002, y within 0.011, x equal;
    // no 0.30 shear (the end radius is 0.20): the whole end offset stays an order of magnitude lower.
    const double dz = s6[2] - segZ(7), dy = s6[1] - yEnd;
    Check(std::fabs(dz) < 0.002, "7th attach z vs string_size_7 end", s6[2], segZ(7));
    Check(std::fabs(dy) < 0.012, "7th attach y vs string_size_7 end", s6[1], yEnd);
    Check(std::sqrt(dz * dz + dy * dy) < 0.05, "7th joint offset", std::sqrt(dz * dz + dy * dy), 0.0);
    if (!fails) std::printf("ok\n");
    return fails ? 1 : 0;
}
