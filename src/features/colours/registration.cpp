// The colour feature's hooks: looked up by src/registry.cpp (Rsxr_FindHook) and written by the main batch, the CM*
// sites only with g_rsxr_native_colours, GLW only with g_rsxr_glow_fix (Rsxr_ShouldPatch).
#include "features/colours/colours.h"
#include "model/gates.h"
#include "model/groups.h"
#include <array>
#include <iterator>

namespace {
constexpr RsxrHook kColourRows[] = {
#include "features/colours/colour_hooks.generated.h"
    // The per-player bass low-B slot (hooks_bass.cpp), with the CM ports that let slot 6 through the getters.
    Rsxr_Mid("CBUV", &Rsxr_Mid_CBUV).Light(),
    Rsxr_Mid("CBRGB", &Rsxr_Mid_CBRGB).Light(),
    Rsxr_Mid("CBSTR", &Rsxr_Mid_CBSTR).Light(),
};
// Every generated colour row and the bass ports share this gate. Apply it here, where the
// registration is owned, without changing the generated ABI/site definitions.
constexpr auto kColourHooks = [] {
    std::array<RsxrHook, std::size(kColourRows)> rows{};

    for (size_t i = 0; i < rows.size(); ++i) {
        rows[i] = kColourRows[i].Gate(kRsxrGateColours);
    }

    return rows;
}();
const RsxrHook kGlowHooks[] = {
    // GLW: RSMods' custom colours give a pure red string a near-black glow (Rsxr_GlowFixPtr).
    Rsxr_Mid("GLW", &Rsxr_Mid_GLW).Gate(kRsxrGateGlow),
};
// The colour manager and the NiTexture list (the ramp swap, d3d/worker.cpp; the headstock textures): data
// references, group Core.
const RsxrHook kRefHooks[] = {
    Rsxr_Ref("X_CM", &g_rsxr_colormgr),
    Rsxr_Ref("X_TCS", &g_rsxr_texlist_cs),
    Rsxr_Ref("X_THEAD", &g_rsxr_texlist_head),
};
}  // namespace

extern const RsxrHookTable kRsxrColourHooks = {Rsxr_Group(RsxrFeature::Colours), kColourHooks.data(),
                                               kColourHooks.size()};
extern const RsxrHookTable kRsxrGlowHooks = {Rsxr_Group(RsxrFeature::Glow), kGlowHooks,
                                             sizeof kGlowHooks / sizeof kGlowHooks[0]};
extern const RsxrHookTable kRsxrColourRefHooks = {Rsxr_Group(RsxrFeature::Core), kRefHooks,
                                                  sizeof kRefHooks / sizeof kRefHooks[0]};
