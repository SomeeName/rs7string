#include "overlay/ui.h"
#include "core/assetdump.h"
#include "core/color.h"
#include "core/frets.h"
#include "core/headstock.h"
#include "core/settings.h"
#include "core/status.h"
#include "core/string7.h"
#include "core/tuner.h"
#include "core/tuner8.h"
#include "core/window.h"
#include "d3d/present.h"
#include "d3d/sehread.h"
#include "d3d/swap.h"
#include "hooks/registry.h"
#include "imgui.h"
#include "log.h"
#include "version.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {
const char* const kTabs[] = {"status", "colour", "neck", "tuner", "debug", "window", "log"};
constexpr int kTabCount = int(sizeof kTabs / sizeof kTabs[0]);
const ImVec4 kGreen(0.45f, 0.90f, 0.45f, 1.0f), kYellow(0.95f, 0.80f, 0.30f, 1.0f), kOrange(1.0f, 0.55f, 0.30f, 1.0f),
    kRed(1.0f, 0.35f, 0.35f, 1.0f), kGrey(0.65f, 0.65f, 0.65f, 1.0f);

const char* KeyName(int vk, char* buf, size_t n) {
    if (vk >= VK_F1 && vk <= VK_F24) {
        std::snprintf(buf, n, "F%d", vk - VK_F1 + 1);
    } else {
        std::snprintf(buf, n, "VK %02X", unsigned(vk));
    }

    return buf;
}

void Help(const Rs7SetDef& d) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        return;
    }

    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(d.help);
    ImGui::TextDisabled("rs7string.ini [%ls] %ls (default %s)", d.section, d.key, d.def[0] ? d.def : "empty");
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void Badge(int i) {
    const Rs7SetDef& d = Rs7Set_Def(i);

    ImGui::SameLine();
    if (d.when == Rs7SetWhen::Live) {
        ImGui::TextColored(kGreen, "[live]");
    } else if (d.when == Rs7SetWhen::NextSession) {
        ImGui::TextColored(kYellow, "[next session]");
    } else {
        ImGui::TextColored(kGrey, "[startup]");
    }
    Help(d);

    if (Rs7Set_NeedsRestart(i)) {
        ImGui::SameLine();
        ImGui::TextColored(kOrange, "restart required (running: %s)",
                           Rs7Set_Format(i, Rs7Set_State(i).running).c_str());
    }

    if (Rs7Set_Dirty(i)) {
        ImGui::SameLine();
        ImGui::TextColored(kYellow, "*");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("changed, not saved (saved: %s)", Rs7Set_Format(i, Rs7Set_State(i).saved).c_str());
        }
    }
}

void LogUiSet(int i, Rs7SetResult r) {
    Rs7Log("overlay: ui set %s = %s -> %s", Rs7Set_Def(i).id, Rs7Set_Format(i, Rs7Set_State(i).value).c_str(),
           Rs7Set_ResultName(r));
}

// One control per setting, typed by the table; every change goes through Rs7Set_Commit (the same path
// as "o set"). Text keys commit on Enter or when the field loses focus.
void Edit(const char* id) {
    const int i = Rs7Set_Find(id);

    if (i < 0) {
        ImGui::TextColored(kRed, "unknown setting %s", id);
        return;
    }

    const Rs7SetDef& d = Rs7Set_Def(i);
    Rs7SetValue v = Rs7Set_State(i).value;
    static Rs7SetResult last[64];
    static bool editing[64];

    ImGui::PushID(i);
    bool changed = false;

    switch (d.type) {
        case Rs7SetType::Bool: {
            bool b = v.i != 0;

            if (ImGui::Checkbox(d.label, &b)) {
                v.i = b ? 1 : 0;
                changed = true;
            }

            break;
        }
        case Rs7SetType::Int:
            changed = ImGui::SliderInt(d.label, &v.i, int(d.lo), int(d.hi));
            break;
        case Rs7SetType::Float:
            changed = ImGui::SliderFloat(d.label, &v.f[0], d.lo, d.hi, "%.3f");
            break;
        case Rs7SetType::Rgb: {
            float c[3];

            Rs7_RgbToFloats(v.rgb, c);

            if (ImGui::ColorEdit3(d.label, c, ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_InputRGB)) {
                uint32_t rgb = 0;

                for (float f : c) {
                    rgb = (rgb << 8) | uint32_t(f <= 0 ? 0 : f >= 1 ? 255 : int(f * 255.0f + 0.5f));
                }

                v.rgb = rgb;
                changed = true;
            }

            break;
        }
        case Rs7SetType::Text: {
            static char bufs[64][kRs7SetTextMax];
            char* buf = bufs[i % 64];

            if (!editing[i % 64]) {
                std::snprintf(buf, kRs7SetTextMax, "%s", v.text);
            }
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
            const bool enter = ImGui::InputText(d.label, buf, kRs7SetTextMax, ImGuiInputTextFlags_EnterReturnsTrue);

            editing[i % 64] = ImGui::IsItemActive();

            if (enter || ImGui::IsItemDeactivatedAfterEdit()) {
                const Rs7SetResult r = Rs7Set_SetText(d.id, buf);

                if (r != Rs7SetResult::Unchanged) {
                    LogUiSet(i, r);
                }
            }

            break;
        }
    }

    if (changed) {
        const Rs7SetResult r = Rs7Set_Commit(i, v);

        if (r != Rs7SetResult::Unchanged) {
            last[i % 64] = r;
        }
        // Sliders change every frame while dragged: log the bool flips at once, the rest when released.
        if (d.type == Rs7SetType::Bool && r != Rs7SetResult::Unchanged) {
            LogUiSet(i, r);
        }
    }

    if (d.type != Rs7SetType::Bool && d.type != Rs7SetType::Text && ImGui::IsItemDeactivatedAfterEdit()) {
        LogUiSet(i, last[i % 64]);
    }
    Help(d);
    Badge(i);
    ImGui::PopID();
}

void Row(const char* name, const char* fmt, ...) {
    ImGui::TextColored(kGrey, "%s", name);
    ImGui::SameLine(ImGui::GetFontSize() * 11.0f);
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    ImGui::TextUnformatted(buf);
}

void OnOff(const char* name, bool on, const char* detail) {
    ImGui::TextColored(kGrey, "%s", name);
    ImGui::SameLine(ImGui::GetFontSize() * 11.0f);
    ImGui::TextColored(on ? kGreen : kGrey, "%s", on ? "ON" : "off");

    if (detail && *detail) {
        ImGui::SameLine();
        ImGui::TextUnformatted(detail);
    }
}

int ColourMode() {  // the colour manager's mode for player 0 (0 normal, 1 colourblind), -1 unknown
    const Rs7SehReader mem{};
    uint32_t mgr = 0, mode = 0;
    if (!g_rs7_colormgr || !mem.Read(g_rs7_colormgr, &mgr, 4) || !mgr || !mem.Read(mgr + 0x348, &mode, 4)) {
        return -1;
    }

    return mode <= 1 ? int(mode) : -1;
}

void Section(const char* title) {
    ImGui::Spacing();
    ImGui::SeparatorText(title);
}

void TabStatus(const Rs7OverlayUi& ui) {
    Section("rs7string");
    Row("build", "%s (%s %s)", RS7_VERSION, __DATE__, __TIME__);
    const Rs7StartStatus st = Rs7Status_Start();

    if (st.state == Rs7StartState::Patched) {
        Row("sites", "%u resolved, %u written (memory protection via %s)", st.resolved, st.written, st.protect);
    } else if (st.state == Rs7StartState::Failed) {
        ImGui::TextColored(kGrey, "sites");
        ImGui::SameLine(ImGui::GetFontSize() * 11.0f);
        ImGui::TextColored(kRed, "NOTHING PATCHED: %s", st.error);
    } else {
        Row("sites", "starting");
    }

    Section("song");
    const Rs7SongStatus s = Rs7Status_Song();

    if (!s.songs) {
        Row("last song setup", "none yet");
    } else {
        const int strings = s.active ? 7 : s.bass ? 4 : 6;

        ImGui::TextColored(kGrey, "7-string mode");
        ImGui::SameLine(ImGui::GetFontSize() * 11.0f);
        if (s.active) {
            ImGui::TextColored(kGreen, "ON (low B = MIDI %d, t6 = %d)", 35 + s.t6, s.t6);
        } else {
            ImGui::TextColored(kGrey, "off");
        }
        Row("strings", "%d (%d SNG tuning entries%s)", strings, s.entries, s.bass ? ", bass" : "");
        char tun[96] = {};
        size_t n = 0;

        for (int k = 0; k < s.entries && k < 8 && n + 8 < sizeof tun; ++k) {
            n += size_t(std::snprintf(tun + n, sizeof tun - n, "%s%d", k ? " " : "", int(s.tuning[k])));
        }

        Row("tuning offsets", "%s", tun);
        Row("song setups", "%ld this run", s.songs);
    }

    const int cm = ColourMode();

    Row("colour mode", "%s", cm == 0 ? "normal" : cm == 1 ? "colourblind" : "unknown");

    Section("features");
    Rs7SwapStats sw{};

    Rs7Swap_Stats(&sw);
    char b[160];

    std::snprintf(b, sizeof b, "hooks %s, %u copies, candidates: %u active / %u refused / %u retry / %u pending",
                  sw.installed ? "on" : "off", sw.copies, sw.active, sw.refused, sw.retry, sw.pending);
    OnOff("ramp copy (V6)", g_rs7_v6[0] != 0.0f, b);
    Rs7S7Stats s7{};

    Rs7S7_Stats(&s7);
    std::snprintf(b, sizeof b, "%d GuitarStrings, %d guitar(s) marked 7-string", s7.guitars, s7.sevenGuitars);
    OnOff("7th string line", g_rs7_s7_on != 0, b);
    Rs7FretsStats fr{};

    Rs7Frets_Stats(&fr);
    std::snprintf(b, sizeof b, "%d guitar(s) applied, L = %.3f", fr.applied, double(g_rs7_fret_halflen));
    OnOff("fret extend", g_rs7_frets_on != 0, b);

    {
        Rs7HeadStats hs{};

        Rs7Head_Stats(&hs);
        std::snprintf(b, sizeof b, "native PegSet: %d built, %d loaded with 7 pegs, %d with peg 7 active%s", hs.pegsets,
                      hs.loaded7, hs.active7, hs.liveOwned ? ", experiments from the overlay" : "");
    }

    OnOff("7th tuning peg", g_rs7_head_on != 0, b);
    Rs7TunerStats ts{};

    Rs7Tuner_Stats(&ts);
    std::snprintf(b, sizeof b, "%d native session(s)", ts.sessions);
    OnOff("7-string tuner", g_rs7_native_tuner != 0, b);
    OnOff("diagnostics", g_rs7_diag != 0, "");

    Section("overlay");
    char k[16];

    Row("hotkey", "%s", KeyName(ui.hotkey, k, sizeof k));
    Row("frames", "%lu Present calls, %lu callback faults", Rs7Present_Frames(), Rs7Present_Faults());
    Row("window", "%p, window thread %lu, render thread %lu", static_cast<void*>(ui.hwnd), ui.wndTid, ui.renderTid);
    Row("focus", "%s",
        GetForegroundWindow() == ui.hwnd ? "game window is foreground (mouse input on)"
                                         : "not foreground (no mouse/keyboard input)");
    Row("background mode", "%s", Rs7Window_Background() ? "on (pipe commands)" : "off");
    Row("RSMods", "%s", GetModuleHandleW(L"xinput1_3.dll") ? "loaded (xinput1_3.dll)" : "not loaded");
    Row("ini", "%ls", ui.iniPath);
}

void TabColour() {
    Section("string 6 colour");
    Edit("color.string6_color");
    Edit("color.string6_color_cb");
    Rs7SwapStats sw{};

    Rs7Swap_Stats(&sw);
    const int cm = ColourMode();

    Row("ramp copy", "%s; copies hold %06X / %06X",
        g_rs7_v6[0] != 0.0f ? "V6 ON (gems use the copies)" : "V6 off (gems show the preview row)", unsigned(sw.rgb[0]),
        unsigned(sw.rgb[1]));
    Row("colour mode", "%s", cm == 0 ? "normal" : cm == 1 ? "colourblind" : "unknown");
    ImGui::TextWrapped("Colour changes reach sparks, hit/miss text, the 7th string line, its glow and the tuner at "
                       "once; the gem ramps are repainted at the next texture bind.");
    Section("ramp copy (Step 2)");
    Edit("color.ramp_swap");
    Edit("color.allow_inexact");
}

void TabNeck() {
    Section("7th string line");
    Edit("neck.string7_line");
    Edit("neck.string7_v");
    Section("fret wires");
    Edit("neck.fret_extend");
    Edit("neck.fret_halflen");
    Section("7-string headstock model");
    Edit("assets.headstock7");
    Edit("assets.headstock7_file");
    Edit("assets.headstock7_wood");
    Edit("assets.dir");
    Edit("assets.suffix");
    Section("7th tuning peg");
    Edit("neck.headstock_peg");
    Section("headstock experiments");
    Edit("debug.head_cull");
    Edit("debug.head_stretch");
    Edit("debug.head_stretch_mode");
}

void TabTuner() {
    Section("7-string tuner");
    Edit("debug.tuner_autotune");
    Rs7TunerStats ts{};

    Rs7Tuner_Stats(&ts);
    Row("sessions", "%d native session(s) now, tuner hooks %s", ts.sessions, ts.any ? "active" : "idle");
    Section("7-string tuning names");
    Edit("tuner.names");
    Edit("tuner.name_prefix");
    Edit("tuner.name_suffix");
}

void TabDebug(Rs7OverlayUi& ui) {
    Section("diagnostics");
    Edit("debug.diagnostics");
    Edit("debug.song_end_log");
    Section("asset dump");
    Edit("debug.dump_assets");
    Edit("debug.dump_dir");
    const Rs7AssetDumpStatus ds = Rs7AssetDump_Status();
    static const char* const kStates[] = {"idle", "waiting for the game's file system", "running", "done",
                                          "FAILED (see rs7string.log)"};

    Row("dump", "%s, %d of %d archives, %d files", kStates[ds.state >= 0 && ds.state <= 4 ? ds.state : 0],
        ds.archivesDone, ds.archives, ds.files);
    const unsigned long frame = static_cast<unsigned long>(ImGui::GetFrameCount());
    const bool armed = ui.dumpArmedFrame && frame - ui.dumpArmedFrame < 240;

    ImGui::BeginDisabled(ds.state != 0);

    if (ImGui::Button(armed ? "Click again to dump every .psarc now" : "Dump assets now")) {
        if (!armed) {
            ui.dumpArmedFrame = frame;
        } else {
            ui.dumpArmedFrame = 0;
            const bool ok = Rs7AssetDump_Trigger(ui.iniPath, ui.gameDir);

            Rs7Log("overlay: ui dump -> %s", ok ? "asset dump started" : "a dump was already started this run");
        }
    }

    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(game's own PSARC reader; many GB; one dump per run)");
}

void TabWindow() {
    Section("window (test driving)");
    Edit("window.background");
    Edit("window.screen");
    Section("overlay");
    Edit("overlay.enabled");
    Edit("overlay.hotkey");
    Edit("overlay.open");
    Edit("overlay.scale");
}

void TabLog(Rs7OverlayUi& ui) {
    static char lines[200][240];
    static unsigned count = 0;
    static unsigned long seq = ~0ul;
    unsigned got = 0;
    char tmp[1][240];
    // Copy the tail only when something was logged (the counter changed).
    const unsigned long now = Rs7Log_Tail(tmp, 0, &got);

    if (now != seq) {
        seq = Rs7Log_Tail(lines, 200, &count);
    }
    ImGui::Checkbox("follow", &ui.logFollow);
    ImGui::SameLine();
    ImGui::TextDisabled("last %u lines of rs7string.log", count);
    ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);

    for (unsigned i = 0; i < count; ++i) {
        const size_t n = strnlen(lines[i], sizeof lines[i]);

        ImGui::TextUnformatted(lines[i], lines[i] + (n && lines[i][n - 1] == '\n' ? n - 1 : n));
    }

    if (ui.logFollow) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}
}  // namespace

int Rs7OverlayUi_TabIndex(const char* name) {
    if (!name) {
        return -1;
    }
    if (!std::strcmp(name, "color")) {
        return 1;
    }

    for (int i = 0; i < kTabCount; ++i) {
        if (!std::strcmp(name, kTabs[i])) {
            return i;
        }
    }

    return -1;
}

void Rs7OverlayUi_Draw(Rs7OverlayUi& ui, bool* open) {
    const float f = ImGui::GetFontSize();

    ImGui::SetNextWindowPos(ImVec2(f * 1.5f, f * 1.5f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(f * 58.0f, f * 36.0f), ImGuiCond_FirstUseEver);
    char k[16];
    char title[64];

    std::snprintf(title, sizeof title, "rs7string %s  (%s)###rs7overlay", RS7_VERSION, KeyName(ui.hotkey, k, sizeof k));

    if (!ImGui::Begin(title, open)) {
        ImGui::End();
        return;
    }

    const int dirty = Rs7Set_DirtyCount();

    ImGui::BeginDisabled(dirty == 0);
    char save[64];

    std::snprintf(save, sizeof save, "Save to rs7string.ini (%d changed)", dirty);

    if (ImGui::Button(save)) {
        const int n = Rs7Set_Save(ui.iniPath);
        char r[96];

        std::snprintf(r, sizeof r, n < 0 ? "save FAILED (see log)" : "%d key(s) saved", n);
        ui.saveResult = r;
        Rs7Log("overlay: ui save -> %d keys written to %ls", n, ui.iniPath);
    }

    ImGui::EndDisabled();
    int restart = 0;

    for (int i = 0; i < Rs7Set_Count(); ++i) {
        restart += Rs7Set_NeedsRestart(i) ? 1 : 0;
    }

    if (!ui.saveResult.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ui.saveResult.c_str());
    }

    if (restart) {
        ImGui::SameLine();
        ImGui::TextColored(kOrange, "%d change(s) need a game restart", restart);
    }

    if (ImGui::BeginTabBar("tabs")) {
        static const char* const kLabels[] = {"Status", "Colour", "Neck", "Tuner", "Debug", "Window", "Log"};

        for (int t = 0; t < kTabCount; ++t) {
            const ImGuiTabItemFlags fl = ui.tabRequest == t ? ImGuiTabItemFlags_SetSelected : 0;
            if (!ImGui::BeginTabItem(kLabels[t], nullptr, fl)) {
                continue;
            }

            switch (t) {
                case 0:
                    TabStatus(ui);
                    break;
                case 1:
                    TabColour();
                    break;
                case 2:
                    TabNeck();
                    break;
                case 3:
                    TabTuner();
                    break;
                case 4:
                    TabDebug(ui);
                    break;
                case 5:
                    TabWindow();
                    break;
                case 6:
                    TabLog(ui);
                    break;
            }

            ImGui::EndTabItem();
        }

        ui.tabRequest = -1;
        ImGui::EndTabBar();
    }

    ImGui::End();
}
