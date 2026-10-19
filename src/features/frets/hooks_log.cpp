// The fret bars' log lines (hooks.h): noinline helpers reached only on 7-string events or once per song, so the
// stock path of hooks.cpp carries no exception frames.
#include "features/frets/hooks.h"
#include "features/scene/scene.h"
#include "platform/log.h"
#include <cmath>

namespace {
// One bar's root node and its first NiMesh: local / world transform and bounds, and the mesh
// bound's span along the string axis u next to strings 5, 0 and 6 (the bound sphere is an upper
// bound of the real wire length).
__declspec(noinline) void LogBar(const char* tag, int f, uint8_t* entity, const float* u, const float* w0,
                                 const float* w1) {
    uint8_t* root = rs7s7d::EntityRoot(entity);
    float lt[3] = {}, ls = 0, wt[3] = {}, ws = 0, rb[4] = {};

    if (root) {
        rs7s7d::SafeCopy(lt, root + 0x70, 12);
        rs7s7d::SafeCopy(&ls, root + 0x7C, 4);
        rs7s7d::SafeCopy(wt, root + 0xA4, 12);
        rs7s7d::SafeCopy(&ws, root + 0xB0, 4);
        rs7s7d::SafeCopy(rb, root + 0x2C, 16);
    }

    // Depth-first search for the first NiMesh (fretShape).
    uint8_t* stack[64];
    int sp = 0, visited = 0;
    uint8_t* mesh = nullptr;

    if (root) {
        stack[sp++] = root;
    }

    while (sp && !mesh && visited < 64) {
        uint8_t* n = stack[--sp];

        ++visited;

        if (rs7s7d::IsKindOf(n, "NiMesh")) {
            mesh = n;
            break;
        }

        if (!rs7s7d::IsKindOf(n, "NiNode")) {
            continue;
        }

        uint8_t* arr = nullptr;
        uint16_t slots = 0;

        rs7s7d::SafeCopy(&arr, n + 0xD8, 4);
        rs7s7d::SafeCopy(&slots, n + 0xDC, 2);

        for (int i = int(slots) - 1; arr && i >= 0 && sp < 64; --i) {
            uint8_t* c = nullptr;

            rs7s7d::SafeCopy(&c, arr + i * 4, 4);
            if (c) {
                stack[sp++] = c;
            }
        }
    }

    float mb[4] = {}, mw[4] = {}, ms = 0;
    char name[48] = {};

    if (mesh) {
        rs7s7d::SafeCopy(mw, mesh + 0x2C, 16);
        rs7s7d::SafeCopy(mb, mesh + 0xF0, 16);
        rs7s7d::SafeCopy(&ms, mesh + 0xB0, 4);
        rs7s7d::NameOfNode(mesh, name, sizeof name);
    }

    auto dot = [&](const float* p) {
        return p[0] * u[0] + p[1] * u[1] + p[2] * u[2];
    };
    float s5[3], s6[3];

    for (int i = 0; i < 3; ++i) {
        s5[i] = w0[i] - 5.0f * (w0[i] - w1[i]);
        s6[i] = 2.0f * w0[i] - w1[i];
    }

    const float c = dot(mw);

    try {
        Rs7Log("frets %s: bar %d entity %p root %p: local t=(%.3f, %.3f, %.3f) s=%.4f, world t=(%.3f, %.3f, %.3f) "
               "s=%.4f, root bound c=(%.3f, %.3f, %.3f) r=%.3f; mesh %p \"%s\" world bound c=(%.3f, %.3f, %.3f) "
               "r=%.3f world s=%.4f, model bound c=(%.3f, %.3f, %.3f) r=%.3f; along the strings: mesh bound "
               "%.3f .. %.3f, string 5 %.3f, string 0 %.3f, string 6 %.3f",
               tag, f, entity, root, double(lt[0]), double(lt[1]), double(lt[2]), double(ls), double(wt[0]),
               double(wt[1]), double(wt[2]), double(ws), double(rb[0]), double(rb[1]), double(rb[2]), double(rb[3]),
               mesh, name, double(mw[0]), double(mw[1]), double(mw[2]), double(mw[3]), double(ms), double(mb[0]),
               double(mb[1]), double(mb[2]), double(mb[3]), double(c - mw[3]), double(c + mw[3]), double(dot(s5)),
               double(dot(w0)), double(dot(s6)));
    } catch (...) {
    }
}
}  // namespace

namespace rs7fretglue {
using namespace rs7frets;

__declspec(noinline) void LogApply(const char* by, const uint8_t* guitar, const uint8_t* grid, int bars, int set,
                                   const Extend& x, const float* mid0, const float* new0) {
    try {
        Rs7Log("frets: %s applied (guitar %p, FretGrid %p): %d of %d bars re-placed and scaled; string spacing "
               "d=%.4f, shift=(%.4f, %.4f, %.4f) = d/2 towards string 6, scale k=%.4f (L=%.3f); bar 0 midpoint "
               "(%.3f, %.3f, %.3f) -> (%.3f, %.3f, %.3f)",
               by, guitar, grid, set, bars, double(x.spacing), double(x.shift[0]), double(x.shift[1]),
               double(x.shift[2]), double(x.scale), double(g_rs7_fret_halflen), double(mid0[0]), double(mid0[1]),
               double(mid0[2]), double(new0[0]), double(new0[1]), double(new0[2]));
    } catch (...) {
    }
}

__declspec(noinline) void LogRestore(const uint8_t* guitar, int set) {
    try {
        Rs7Log("frets: restored (guitar %p): %d bars back on the stock midpoint, scale 1 (fret_extend switched off "
               "(overlay))",
               guitar, set);
    } catch (...) {
    }
}

__declspec(noinline) void LogFail(const char* why, const uint8_t* guitar) {
    try {
        Rs7Log("frets: NOT applied (guitar %p): %s", guitar, why);
    } catch (...) {
    }
}

__declspec(noinline) void LogReadBack(const char* tag, uint8_t* guitar, uint8_t* grid) {
    const float *a0, *a1, *w0, *w1;
    if (!grid || !Attach(guitar, &a0, &a1, &w0, &w1)) {
        return;
    }

    float d[3], len = 0;

    for (int i = 0; i < 3; ++i) {
        d[i] = a0[i] - a1[i];
        len += d[i] * d[i];
    }

    len = std::sqrt(len);
    if (!(len > 1e-3f)) {
        return;
    }

    for (float& v : d) {
        v /= len;
    }

    const int bars = BarCount(guitar);
    const int pick[3] = {0, 1, 12};

    for (int f : pick) {
        if (f < bars) {
            if (uint8_t* e = At<uint8_t*>(Bar(grid, f), kBarEntity)) {
                LogBar(tag, f, e, d, w0, w1);
            }
        }
    }
}

__declspec(noinline) void LogInlay(const uint8_t* guitar, const float* p, float dz) {
    try {
        Rs7Log("frets: IN1 inlays of guitar %p centred on 7 strings: entity at (%.3f, %.3f, %.3f) (string axis %+.4f)",
               guitar, double(p[0]), double(p[1]), double(p[2]), double(dz));
    } catch (...) {
    }
}

__declspec(noinline) void LogFloor(const uint8_t* guitar, const float* before, const float* shift, int n) {
    try {
        Rs7Log(
            "frets: highway floor of guitar %p (the fret-wire bottom ends lie on string 6's side: inverted strings): "
            "%d ends moved by (%.4f, %.4f, %.4f); bar 0 end (%.3f, %.3f, %.3f) -> (%.3f, %.3f, %.3f)",
            guitar, n, double(shift[0]), double(shift[1]), double(shift[2]), double(before[0]), double(before[1]),
            double(before[2]), double(before[0] + shift[0]), double(before[1] + shift[1]),
            double(before[2] + shift[2]));
    } catch (...) {
    }
}
}  // namespace rs7fretglue
