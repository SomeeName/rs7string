#include "overlay/ui.h"
#include "d3d/present.h"
#include "features/colours/colours.h"
#include "features/window/window.h"
#include "imgui.h"
#include "model/status.h"
#include "platform/log.h"
#include "platform/settings.h"
#include "platform/uilayout.h"
#include "version.h"
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {
// The tabs, by index: the first ones are the Rs7UiTab values (platform/uilayout.h), the log tab follows them.
struct Tab {
    const char* name;  // "o tab <name>"
    const char* label;
};

constexpr Tab kTabs[] = {{"status", "Status"}, {"colour", "Colour"}, {"neck", "Neck"}, {"tuner", "Tuner"},
                         {"debug", "Debug"},   {"window", "Window"}, {"log", "Log"}};
constexpr int kTabCount = int(sizeof kTabs / sizeof kTabs[0]);
constexpr int kLogTab = int(Rs7UiTab::Window) + 1;

constexpr bool TabIs(Rs7UiTab t, const char* name) {
    const char* a = kTabs[int(t)].name;

    while (*a && *a == *name) {
        ++a;
        ++name;
    }

    return *a == *name;
}

static_assert(TabIs(Rs7UiTab::Status, "status") && TabIs(Rs7UiTab::Colour, "colour") && TabIs(Rs7UiTab::Neck, "neck") &&
                  TabIs(Rs7UiTab::Tuner, "tuner") && TabIs(Rs7UiTab::Debug, "debug") &&
                  TabIs(Rs7UiTab::Window, "window"),
              "kTabs follows Rs7UiTab");
static_assert(kLogTab == kTabCount - 1, "the log tab is the last one");
// The one restart note: restart-only settings carry " *" in their label (Edit).
constexpr char kRestartNote[] = "Settings marked * apply after you restart the game.";
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

// The state after a control. Restart-only keys carry " *" in their label (kRestartNote) instead of a tag; the player
// view shows no tags and no ini values.
void Badge(int i, bool player) {
    const Rs7SetDef& d = Rs7Set_Def(i);

    if (!player && d.when != Rs7SetWhen::Restart) {
        ImGui::SameLine();
        if (d.when == Rs7SetWhen::Live) {
            ImGui::TextColored(kGreen, "[live]");
        } else {
            ImGui::TextColored(kYellow, "[next session]");
        }
        Help(d);
    }

    if (Rs7Set_NeedsRestart(i)) {
        ImGui::SameLine();
        if (player) {
            ImGui::TextColored(kOrange, "restart the game to apply");
        } else {
            ImGui::TextColored(kOrange, "restart required (running: %s)",
                               Rs7Set_Format(i, Rs7Set_State(i).running).c_str());
        }
    }

    if (Rs7Set_Dirty(i)) {
        ImGui::SameLine();
        ImGui::TextColored(kYellow, "not saved");
        if (!player && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("changed, not saved (saved: %s)", Rs7Set_Format(i, Rs7Set_State(i).saved).c_str());
        }
    }
}

// The player view's hint: a "(?)" after the control with the hint as its tooltip.
void HintMarker(const char* hint) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");

    if (!ImGui::IsItemHovered()) {
        return;
    }

    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(hint);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// After a control: the table's help as a tooltip and the tags, or in the player view the "(?)" hint and the badge. A
// locked control keeps its reason as text below, because that text is what explains the disabled control.
void Decorate(const Rs7UiItem& it, int i, bool locked) {
    const bool player = it.tab == Rs7UiTab::Player;

    if (!player) {
        Help(Rs7Set_Def(i));
    } else if (it.hint) {
        HintMarker(it.hint);
    }

    Badge(i, player);

    if (locked && it.lockedHint) {
        ImGui::TextDisabled("%s", it.lockedHint);
    }
}

void LogUiSet(int i, Rs7SetResult r) {
    Rs7Log("overlay: ui set %s = %s -> %s", Rs7Set_Def(i).id, Rs7Set_Format(i, Rs7Set_State(i).value).c_str(),
           Rs7Set_ResultName(r));
}

// A colour as #RRGGBB; its width leaves room for the label.
bool EditRgb(const char* label, Rs7SetValue& v) {
    float c[3];

    Rs7_RgbToFloats(v.rgb, c);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);

    if (!ImGui::ColorEdit3(label, c, ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_InputRGB)) {
        return false;
    }

    uint32_t rgb = 0;

    for (float f : c) {
        rgb = (rgb << 8) | uint32_t(f <= 0 ? 0 : f >= 1 ? 255 : int(f * 255.0f + 0.5f));
    }

    v.rgb = rgb;

    return true;
}

// A player slider is narrow, so its label, "(?)" and badges fit the line.
void NarrowFor(bool player) {
    if (player) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    }
}

// One control per setting, typed by the table; every change goes through Rs7Set_Commit (the same path
// as "o set"). Text keys commit on Enter or when the field loses focus. A Player item brings its own plain label and
// hint (platform/uilayout.h); other items use the table's label and help.
void Edit(const Rs7UiItem& it) {
    const char* id = it.text;
    const bool player = it.tab == Rs7UiTab::Player;
    const int i = Rs7Set_Find(id);

    if (i < 0) {
        ImGui::TextColored(kRed, "unknown setting %s", id);
        return;
    }

    const Rs7SetDef& d = Rs7Set_Def(i);
    Rs7SetValue v = Rs7Set_State(i).value;
    static Rs7SetResult last[64];
    static bool editing[64];
    char label[160];

    std::snprintf(label, sizeof label, "%s%s", player && it.label ? it.label : d.label,
                  d.when == Rs7SetWhen::Restart ? " *" : "");
    ImGui::PushID(i);
    bool changed = false;
    const bool locked = player && it.locked && it.locked();

    ImGui::BeginDisabled(locked);

    switch (d.type) {
        case Rs7SetType::Bool: {
            bool b = v.i != 0;

            if (ImGui::Checkbox(label, &b)) {
                v.i = b ? 1 : 0;
                changed = true;
            }

            break;
        }
        case Rs7SetType::Int:
            NarrowFor(player);
            changed = ImGui::SliderInt(label, &v.i, int(d.lo), int(d.hi));
            break;
        case Rs7SetType::Float:
            NarrowFor(player);
            changed = ImGui::SliderFloat(label, &v.f[0], d.lo, d.hi,
                                         !player                         ? "%.3f"
                                         : it.zeroText && v.f[0] == 0.0f ? it.zeroText
                                                                         : "%.2f");
            break;
        case Rs7SetType::Rgb:
            changed = EditRgb(label, v);
            break;
        case Rs7SetType::Text: {
            static char bufs[64][kRs7SetTextMax];
            char* buf = bufs[i % 64];

            if (!editing[i % 64]) {
                std::snprintf(buf, kRs7SetTextMax, "%s", v.text);
            }
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
            const bool enter = ImGui::InputText(label, buf, kRs7SetTextMax, ImGuiInputTextFlags_EnterReturnsTrue);

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

    ImGui::EndDisabled();

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
    Decorate(it, i, locked);
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

void Section(const char* title) {
    ImGui::Spacing();
    ImGui::SeparatorText(title);
}

// The features' items of one tab (platform/uilayout.h), in list order.
void Items(Rs7UiTab tab) {
    const Rs7UiItem* items[kRs7UiMaxTabItems];
    int n = Rs7Ui_TabItems(tab, items, kRs7UiMaxTabItems);

    n = n < kRs7UiMaxTabItems ? n : kRs7UiMaxTabItems;

    for (int i = 0; i < n; ++i) {
        const Rs7UiItem& it = *items[i];
        char buf[kRs7UiRowBuf];

        switch (it.kind) {
            case Rs7UiKind::Section:
                Section(it.text);
                break;
            case Rs7UiKind::Setting:
                Edit(it);
                break;
            case Rs7UiKind::Row:
                it.live(buf, sizeof buf);
                Row(it.text, "%s", buf);
                break;
            case Rs7UiKind::OnOff: {
                const bool on = it.live(buf, kRs7UiOnOffBuf);

                OnOff(it.text, on, buf);
                break;
            }
            case Rs7UiKind::Note:
                ImGui::TextWrapped("%s", it.text);
                break;
        }
    }
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

    Row("colour mode", "%s", Rs7Colour_ModeName());

    Section("features");
    Items(Rs7UiTab::Status);

    Section("overlay");
    char k[16];

    Row("hotkey", "%s", KeyName(ui.hotkey, k, sizeof k));
    Row("frames", "%lu Present calls, %lu callback faults", Rs7Present_Frames(), Rs7Present_Faults());
    Row("window", "%p, window thread %lu, render thread %lu", static_cast<void*>(ui.hwnd), ui.wndTid, ui.renderTid);
    Row("focus", "%s",
        GetForegroundWindow() == ui.hwnd ? "game window is foreground (mouse input on)"
                                         : "game window not in foreground");
    Row("background mode", "%s", Rs7Window_Background() ? "on (pipe commands)" : "off");
    Row("RSMods", "%s", GetModuleHandleW(L"xinput1_3.dll") ? "loaded (xinput1_3.dll)" : "not loaded");
    Row("ini", "%ls", ui.iniPath);
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

// Save button, its result and the restart banner.
void SaveBar(Rs7OverlayUi& ui) {
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
        ImGui::TextColored(kOrange, "%d change(s) apply after you restart the game", restart);
    }
}

// The technical tabs, inside the Advanced tab.
void TechnicalTabs(Rs7OverlayUi& ui) {
    if (!ImGui::BeginTabBar("tabs")) {
        return;
    }

    for (int t = 0; t < kTabCount; ++t) {
        const ImGuiTabItemFlags fl = ui.tabRequest == t ? ImGuiTabItemFlags_SetSelected : 0;
        if (!ImGui::BeginTabItem(kTabs[t].label, nullptr, fl)) {
            continue;
        }

        switch (t) {
            case 0:
                TabStatus(ui);
                break;
            case kLogTab:
                TabLog(ui);
                break;
            default:
                Items(Rs7UiTab(t));
                break;
        }

        ImGui::EndTabItem();
    }

    ui.tabRequest = -1;
    ImGui::EndTabBar();
}

// The Settings of one player tab.
void PlayerTab(const Rs7UiPlayerTab& tab) {
    const Rs7UiItem* items[kRs7UiMaxTabItems];
    int n = Rs7Ui_TabItems(Rs7UiTab::Player, items, kRs7UiMaxTabItems);

    n = n < kRs7UiMaxTabItems ? n : kRs7UiMaxTabItems;

    for (int i = tab.first; i < tab.first + tab.count && i < n; ++i) {
        Edit(*items[i]);
    }
}

// The overlay's one tab bar: the player tabs (from the tables), then Advanced with the technical tabs.
void MainTabs(Rs7OverlayUi& ui, const Rs7UiPlayerTab* tabs, int n) {
    if (!ImGui::BeginTabBar("main")) {
        return;
    }

    for (int t = 0; t <= n; ++t) {
        const bool advanced = t == n;
        const ImGuiTabItemFlags fl = ui.mainRequest == t ? ImGuiTabItemFlags_SetSelected : 0;

        if (!ImGui::BeginTabItem(advanced ? "Advanced" : tabs[t].label, nullptr, fl)) {
            continue;
        }

        if (advanced) {
            TechnicalTabs(ui);
        } else {
            PlayerTab(tabs[t]);
        }

        ImGui::EndTabItem();
    }

    ui.mainRequest = -1;
    ImGui::EndTabBar();
}

// The same name, ignoring case; a space and '-' are the same.
bool SameName(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        const int x = *a == ' ' ? '-' : std::tolower(static_cast<unsigned char>(*a));
        const int y = *b == ' ' ? '-' : std::tolower(static_cast<unsigned char>(*b));

        if (x != y) {
            return false;
        }
    }

    return *a == *b;
}

// The default window height: title bar, save bar, note, tab bar and the longest player tab with its locked hints.
// Computed from the tables, so a tab with more settings makes the window taller instead of making it scroll.
float DefaultHeight(const Rs7UiPlayerTab* tabs, int n) {
    float tallest = 0;

    for (int t = 0; t < n; ++t) {
        tallest = std::max(tallest, float(tabs[t].count) * ImGui::GetFrameHeightWithSpacing() +
                                        float(tabs[t].locked) * ImGui::GetTextLineHeightWithSpacing());
    }

    return ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2 + ImGui::GetFrameHeightWithSpacing() * 2 +
           ImGui::GetTextLineHeightWithSpacing() + tallest;
}
}  // namespace

bool Rs7OverlayUi_RequestTab(Rs7OverlayUi& ui, const char* name) {
    if (!name) {
        return false;
    }

    Rs7UiPlayerTab tabs[kRs7UiMaxPlayerTabs];
    const int n = std::min(Rs7Ui_PlayerTabs(tabs, kRs7UiMaxPlayerTabs), kRs7UiMaxPlayerTabs);
    const bool player = !std::strncmp(name, "player-", 7);

    if (!player) {
        if (SameName(name, "advanced")) {
            ui.mainRequest = n;
            return true;
        }

        for (int i = 0; i < kTabCount; ++i) {
            if (SameName(name, kTabs[i].name) || (i == int(Rs7UiTab::Colour) && SameName(name, "color"))) {
                ui.tabRequest = i;
                ui.mainRequest = n;
                return true;
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        if (SameName(name + (player ? 7 : 0), tabs[i].label)) {
            ui.mainRequest = i;
            return true;
        }
    }

    return false;
}

void Rs7OverlayUi_Draw(Rs7OverlayUi& ui, bool* open) {
    const float f = ImGui::GetFontSize();
    Rs7UiPlayerTab tabs[kRs7UiMaxPlayerTabs];
    const int n = std::min(Rs7Ui_PlayerTabs(tabs, kRs7UiMaxPlayerTabs), kRs7UiMaxPlayerTabs);

    ImGui::SetNextWindowPos(ImVec2(f * 1.5f, f * 1.5f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(f * 58.0f, DefaultHeight(tabs, n)), ImGuiCond_FirstUseEver);
    char k[16];
    char title[64];

    std::snprintf(title, sizeof title, "rs7string %s  (%s)###rs7overlay", RS7_VERSION, KeyName(ui.hotkey, k, sizeof k));

    if (!ImGui::Begin(title, open)) {
        ImGui::End();
        return;
    }

    SaveBar(ui);
    ImGui::TextDisabled("%s", kRestartNote);
    MainTabs(ui, tabs, n);
    ImGui::End();
}
