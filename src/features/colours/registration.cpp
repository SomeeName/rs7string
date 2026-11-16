// The colour feature's hooks: looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch, the CM*
// sites only with g_rs7_native_colours, GLW only with g_rs7_glow_fix (Rs7_ShouldPatch).
#include "features/colours/colours.h"
#include "model/gates.h"
#include "model/groups.h"
#include <array>
#include <iterator>

namespace {
constexpr Rs7Hook kColourRows[] = {
#include "features/colours/colour_hooks.generated.h"
    // The per-player bass low-B slot (hooks_bass.cpp), with the CM ports that let slot 6 through the getters.
    Rs7_Mid("CBUV", &Rs7_Mid_CBUV).Light(),
    Rs7_Mid("CBRGB", &Rs7_Mid_CBRGB).Light(),
    Rs7_Mid("CBSTR", &Rs7_Mid_CBSTR).Light(),
};
// Every generated colour row and the bass ports share this gate. Apply it here, where the
// registration is owned, without changing the generated ABI/site definitions.
constexpr auto kColourHooks = [] {
    std::array<Rs7Hook, std::size(kColourRows)> rows{};

    for (size_t i = 0; i < rows.size(); ++i) {
        rows[i] = kColourRows[i].Gate(kRs7GateColours);
    }

    return rows;
}();
const Rs7Hook kGlowHooks[] = {
    // GLW: RSMods' custom colours give a pure red string a near-black glow (Rs7_GlowFixPtr).
    Rs7_Mid("GLW", &Rs7_Mid_GLW).Gate(kRs7GateGlow),
};
// The colour manager and the NiTexture list (the ramp swap, d3d/worker.cpp; the headstock textures): data
// references, group Core.
const Rs7Hook kRefHooks[] = {
    Rs7_Ref("X_CM", &g_rs7_colormgr),
    Rs7_Ref("X_TCS", &g_rs7_texlist_cs),
    Rs7_Ref("X_THEAD", &g_rs7_texlist_head),
};
}  // namespace

extern const Rs7HookTable kRs7ColourHooks = {Rs7_Group(Rs7Feature::Colours), kColourHooks.data(), kColourHooks.size()};
extern const Rs7HookTable kRs7GlowHooks = {Rs7_Group(Rs7Feature::Glow), kGlowHooks,
                                           sizeof kGlowHooks / sizeof kGlowHooks[0]};
extern const Rs7HookTable kRs7ColourRefHooks = {Rs7_Group(Rs7Feature::Core), kRefHooks,
                                                sizeof kRefHooks / sizeof kRefHooks[0]};
