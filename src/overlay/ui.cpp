#include "overlay/ui.h"
#include "d3d/present.h"
#include "features/colours/colours.h"
#include "features/window/window.h"
#include "imgui.h"
#include "model/status.h"
#include "model/tuning.h"
#include "platform/log.h"
#include "platform/settings.h"
#include "platform/uilayout.h"
#include "version.h"
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

namespace {
// The tabs, by index: the first ones are the RsxrUiTab values (platform/uilayout.h), the log tab follows them.
struct Tab {
    const char* name;  // "o tab <name>"
    const char* label;
};

constexpr Tab kTabs[] = {{"status", "Status"}, {"colour", "Colour"}, {"neck", "Neck"}, {"tuner", "Tuner"},
                         {"debug", "Debug"},   {"window", "Window"}, {"log", "Log"}};
constexpr int kTabCount = int(sizeof kTabs / sizeof kTabs[0]);
constexpr int kLogTab = int(RsxrUiTab::Window) + 1;
constexpr unsigned kLogLines = 200;     // lines the log tab shows
constexpr float kRowNameWidth = 11.0f;  // status rows: value column, in font sizes

constexpr bool TabIs(RsxrUiTab t, const char* name) {
    const char* a = kTabs[int(t)].name;

    while (*a && *a == *name) {
        ++a;
        ++name;
    }

    return *a == *name;
}

static_assert(TabIs(RsxrUiTab::Status, "status") && TabIs(RsxrUiTab::Colour, "colour") &&
                  TabIs(RsxrUiTab::Neck, "neck") && TabIs(RsxrUiTab::Tuner, "tuner") &&
                  TabIs(RsxrUiTab::Debug, "debug") && TabIs(RsxrUiTab::Window, "window"),
              "kTabs follows RsxrUiTab");
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

// Opens a tooltip with wrapped text; close it with EndWrappedTooltip.
void BeginWrappedTooltip(const char* text) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text);
}

void EndWrappedTooltip() {
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void Help(const RsxrSetDef& d) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        return;
    }

    BeginWrappedTooltip(d.help);
    ImGui::TextDisabled("rsxr.ini [%ls] %ls (default %s)", d.section, d.key, d.def[0] ? d.def : "empty");
    EndWrappedTooltip();
}

// The state after a control. Restart-only keys carry " *" in their label (kRestartNote) instead of a tag; the player
// view shows no tags and no ini values.
void Badge(int i, bool player) {
    const RsxrSetDef& d = RsxrSet_Def(i);

    if (!player && d.when != RsxrSetWhen::Restart) {
        ImGui::SameLine();
        if (d.when == RsxrSetWhen::Live) {
            ImGui::TextColored(kGreen, "[live]");
        } else {
            ImGui::TextColored(kYellow, "[next session]");
        }
        Help(d);
    }

    if (RsxrSet_NeedsRestart(i)) {
        ImGui::SameLine();
        if (player) {
            ImGui::TextColored(kOrange, "restart the game to apply");
        } else {
            ImGui::TextColored(kOrange, "restart required (running: %s)",
                               RsxrSet_Format(i, RsxrSet_State(i).running).c_str());
        }
    }

    if (RsxrSet_Dirty(i)) {
        ImGui::SameLine();
        ImGui::TextColored(kYellow, "not saved");
        if (!player && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("changed, not saved (saved: %s)", RsxrSet_Format(i, RsxrSet_State(i).saved).c_str());
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

    BeginWrappedTooltip(hint);
    EndWrappedTooltip();
}

// After a control: the table's help as a tooltip and the tags, or in the player view the "(?)" hint and the badge. A
// locked control keeps its reason as text below, because that text is what explains the disabled control.
void Decorate(const RsxrUiItem& it, int i, bool locked) {
    const bool player = it.tab == RsxrUiTab::Player;

    if (!player) {
        Help(RsxrSet_Def(i));
    } else if (it.hint) {
        HintMarker(it.hint);
    }

    Badge(i, player);

    if (locked && it.lockedHint) {
        ImGui::TextDisabled("%s", it.lockedHint);
    }
}

void LogUiSet(int i, RsxrSetResult r) {
    RsxrLog("overlay: ui set %s = %s -> %s", RsxrSet_Def(i).id, RsxrSet_Format(i, RsxrSet_State(i).value).c_str(),
            RsxrSet_ResultName(r));
}

// A colour as #RRGGBB; its width leaves room for the label.
bool EditRgb(const char* label, RsxrSetValue& v) {
    float c[3];

    Rsxr_RgbToFloats(v.rgb, c);
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

// Edit state of one setting: the last commit result (logged when a slider is released) and the text field.
struct EditState {
    RsxrSetResult last = RsxrSetResult::Unchanged;
    bool editing = false;
    char text[kRsxrSetTextMax] = {};
};

// One per setting of the store; the count is fixed once the tables are linked.
EditState& EditStateOf(int i) {
    static std::vector<EditState> states = std::vector<EditState>(size_t(RsxrSet_Count()));

    return states[size_t(i)];
}

// One control per setting, typed by the table; every change goes through RsxrSet_Commit (the same path
// as "o set"). Text keys commit on Enter or when the field loses focus. A Player item brings its own plain label and
// hint (platform/uilayout.h); other items use the table's label and help.
void Edit(const RsxrUiItem& it) {
    const char* id = it.text;
    const bool player = it.tab == RsxrUiTab::Player;
    const int i = RsxrSet_Find(id);

    if (i < 0) {
        ImGui::TextColored(kRed, "unknown setting %s", id);
        return;
    }

    const RsxrSetDef& d = RsxrSet_Def(i);
    RsxrSetValue v = RsxrSet_State(i).value;
    EditState& es = EditStateOf(i);
    char label[160];

    std::snprintf(label, sizeof label, "%s%s", player && it.label ? it.label : d.label,
                  d.when == RsxrSetWhen::Restart ? " *" : "");
    ImGui::PushID(i);
    bool changed = false;
    const bool locked = player && it.locked && it.locked();

    ImGui::BeginDisabled(locked);

    switch (d.type) {
        case RsxrSetType::Bool: {
            bool b = v.i != 0;

            if (ImGui::Checkbox(label, &b)) {
                v.i = b ? 1 : 0;
                changed = true;
            }

            break;
        }
        case RsxrSetType::Int:
            NarrowFor(player);
            changed = ImGui::SliderInt(label, &v.i, int(d.lo), int(d.hi));
            break;
        case RsxrSetType::Float:
            NarrowFor(player);
            changed = ImGui::SliderFloat(label, &v.f[0], d.lo, d.hi,
                                         !player                         ? "%.3f"
                                         : it.zeroText && v.f[0] == 0.0f ? it.zeroText
                                                                         : "%.2f");
            break;
        case RsxrSetType::Rgb:
            changed = EditRgb(label, v);
            break;
        case RsxrSetType::Text: {
            char* buf = es.text;

            if (!es.editing) {
                std::snprintf(buf, kRsxrSetTextMax, "%s", v.text);
            }
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
            const bool enter = ImGui::InputText(label, buf, kRsxrSetTextMax, ImGuiInputTextFlags_EnterReturnsTrue);

            es.editing = ImGui::IsItemActive();

            if (enter || ImGui::IsItemDeactivatedAfterEdit()) {
                const RsxrSetResult r = RsxrSet_SetText(d.id, buf);

                if (r != RsxrSetResult::Unchanged) {
                    LogUiSet(i, r);
                }
            }

            break;
        }
    }

    if (ImGui::IsItemActivated()) {
        es.last = RsxrSetResult::Unchanged;  // a drag that changes nothing must not log the previous drag's result
    }
    ImGui::EndDisabled();

    if (changed) {
        const RsxrSetResult r = RsxrSet_Commit(i, v);

        if (r != RsxrSetResult::Unchanged) {
            es.last = r;
        }
        // Sliders change every frame while dragged: log the bool flips at once, the rest when released.
        if (d.type == RsxrSetType::Bool && r != RsxrSetResult::Unchanged) {
            LogUiSet(i, r);
        }
    }

    if (d.type != RsxrSetType::Bool && d.type != RsxrSetType::Text && ImGui::IsItemDeactivatedAfterEdit() &&
        es.last != RsxrSetResult::Unchanged) {
        LogUiSet(i, es.last);
    }
    Decorate(it, i, locked);
    ImGui::PopID();
}

// A status row's grey name; the value follows on the same line.
void RowName(const char* name) {
    ImGui::TextColored(kGrey, "%s", name);
    ImGui::SameLine(ImGui::GetFontSize() * kRowNameWidth);
}

void Row(const char* name, const char* fmt, ...) {
    RowName(name);
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    ImGui::TextUnformatted(buf);
}

void ColouredRow(const char* name, const ImVec4& colour, const char* fmt, ...) {
    RowName(name);
    va_list ap;

    va_start(ap, fmt);
    ImGui::TextColoredV(colour, fmt, ap);
    va_end(ap);
}

void OnOff(const char* name, bool on, const char* detail) {
    RowName(name);
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

// RsxrUi_TabItems, clamped to what fits in `items` (it returns the full count).
int TabItems(RsxrUiTab tab, const RsxrUiItem* (&items)[kRsxrUiMaxTabItems]) {
    const int n = RsxrUi_TabItems(tab, items, kRsxrUiMaxTabItems);

    return n < kRsxrUiMaxTabItems ? n : kRsxrUiMaxTabItems;
}

// The features' items of one tab (platform/uilayout.h), in list order.
void Items(RsxrUiTab tab) {
    const RsxrUiItem* items[kRsxrUiMaxTabItems];
    const int n = TabItems(tab, items);

    for (int i = 0; i < n; ++i) {
        const RsxrUiItem& it = *items[i];
        char buf[kRsxrUiRowBuf];

        switch (it.kind) {
            case RsxrUiKind::Section:
                Section(it.text);
                break;
            case RsxrUiKind::Setting:
                Edit(it);
                break;
            case RsxrUiKind::Row:
                it.live(buf, sizeof buf);
                Row(it.text, "%s", buf);
                break;
            case RsxrUiKind::OnOff: {
                const bool on = it.live(buf, kRsxrUiOnOffBuf);

                OnOff(it.text, on, buf);
                break;
            }
            case RsxrUiKind::Note:
                ImGui::TextWrapped("%s", it.text);
                break;
        }
    }
}

void TabStatus(const RsxrOverlayUi& ui) {
    Section("RS Extended Range");
    Row("build", "%s (%s %s)", RSXR_VERSION, __DATE__, __TIME__);
    const RsxrStartStatus st = RsxrStatus_Start();

    if (st.state == RsxrStartState::Patched) {
        Row("sites", "%u resolved, %u written (memory protection via %s)", st.resolved, st.written, st.protect);
    } else if (st.state == RsxrStartState::Failed) {
        ColouredRow("sites", kRed, "NOTHING PATCHED: %s", st.error);
    } else {
        Row("sites", "starting");
    }

    Section("song");
    const RsxrSongStatus s = RsxrStatus_Song();

    RsxrStatusRow rows[6];
    const int rowCount = RsxrStatus_SongRows(s, rows, int(std::size(rows)));

    for (int i = 0; i < rowCount; ++i) {
        if (rows[i].on) {
            ColouredRow(rows[i].label, kGreen, "%s", rows[i].text);
        } else {
            Row(rows[i].label, "%s", rows[i].text);
        }
    }

    Row("colour mode", "%s", RsxrColour_ModeName());

    Section("features");
    Items(RsxrUiTab::Status);

    Section("overlay");
    char k[16];

    Row("hotkey", "%s", KeyName(ui.hotkey, k, sizeof k));
    Row("frames", "%lu Present calls, %lu callback faults", RsxrPresent_Frames(), RsxrPresent_Faults());
    Row("window", "%p, window thread %lu, render thread %lu", static_cast<void*>(ui.hwnd), ui.wndTid, ui.renderTid);
    Row("focus", "%s",
        GetForegroundWindow() == ui.hwnd ? "game window is foreground (mouse input on)"
                                         : "game window not in foreground");
    Row("background mode", "%s", RsxrWindow_Background() ? "on (pipe commands)" : "off");
    Row("RSMods", "%s", GetModuleHandleW(L"xinput1_3.dll") ? "loaded (xinput1_3.dll)" : "not loaded");
    Row("ini", "%ls", ui.iniPath);
}

void TabLog(RsxrOverlayUi& ui) {
    static char lines[kLogLines][kRsxrLogTailLen];
    static unsigned count = 0;
    static unsigned long seq = ~0ul;
    unsigned got = 0;
    char tmp[1][kRsxrLogTailLen];
    // Copy the tail only when something was logged (the counter changed).
    const unsigned long now = RsxrLog_Tail(tmp, 0, &got);

    if (now != seq) {
        seq = RsxrLog_Tail(lines, kLogLines, &count);
    }
    ImGui::Checkbox("follow", &ui.logFollow);
    ImGui::SameLine();
    ImGui::TextDisabled("last %u lines of rsxr.log", count);
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
void SaveBar(RsxrOverlayUi& ui) {
    const int dirty = RsxrSet_DirtyCount();

    ImGui::BeginDisabled(dirty == 0);
    char save[64];

    std::snprintf(save, sizeof save, "Save to rsxr.ini (%d changed)", dirty);

    if (ImGui::Button(save)) {
        const int n = RsxrSet_Save(ui.iniPath);

        if (n < 0) {
            ui.saveResult = "Could not save rsxr.ini. Is it write-protected? See rsxr.log.";
            RsxrLog("overlay: ui save -> write error, %ls not fully saved", ui.iniPath);
        } else {
            char r[32];

            std::snprintf(r, sizeof r, "%d key(s) saved", n);
            ui.saveResult = r;
            RsxrLog("overlay: ui save -> %d keys written to %ls", n, ui.iniPath);
        }
    }

    ImGui::EndDisabled();
    int restart = 0;

    for (int i = 0; i < RsxrSet_Count(); ++i) {
        restart += RsxrSet_NeedsRestart(i) ? 1 : 0;
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
void TechnicalTabs(RsxrOverlayUi& ui) {
    if (!ImGui::BeginTabBar("tabs")) {
        return;
    }

    for (int t = 0; t < kTabCount; ++t) {
        const ImGuiTabItemFlags fl = ui.tabRequest == t ? ImGuiTabItemFlags_SetSelected : 0;
        if (!ImGui::BeginTabItem(kTabs[t].label, nullptr, fl)) {
            continue;
        }

        switch (t) {
            case int(RsxrUiTab::Status):
                TabStatus(ui);
                break;
            case kLogTab:
                TabLog(ui);
                break;
            default:
                Items(RsxrUiTab(t));
                break;
        }

        ImGui::EndTabItem();
    }

    ui.tabRequest = -1;
    ImGui::EndTabBar();
}

// The Settings of one player tab.
void PlayerTab(const RsxrUiPlayerTab& tab) {
    const RsxrUiItem* items[kRsxrUiMaxTabItems];
    const int n = TabItems(RsxrUiTab::Player, items);

    for (int i = tab.first; i < tab.first + tab.count && i < n; ++i) {
        Edit(*items[i]);
    }
}

// The overlay's one tab bar: the player tabs (from the tables), then Advanced with the technical tabs.
void MainTabs(RsxrOverlayUi& ui, const RsxrUiPlayerTab* tabs, int n) {
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
float DefaultHeight(const RsxrUiPlayerTab* tabs, int n) {
    float tallest = 0;

    for (int t = 0; t < n; ++t) {
        tallest = std::max(tallest, float(tabs[t].count) * ImGui::GetFrameHeightWithSpacing() +
                                        float(tabs[t].locked) * ImGui::GetTextLineHeightWithSpacing());
    }

    return ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2 + ImGui::GetFrameHeightWithSpacing() * 2 +
           ImGui::GetTextLineHeightWithSpacing() + tallest;
}
}  // namespace

bool RsxrOverlayUi_RequestTab(RsxrOverlayUi& ui, const char* name) {
    if (!name) {
        return false;
    }

    RsxrUiPlayerTab tabs[kRsxrUiMaxPlayerTabs];
    const int n = std::min(RsxrUi_PlayerTabs(tabs, kRsxrUiMaxPlayerTabs), kRsxrUiMaxPlayerTabs);
    const bool player = !std::strncmp(name, "player-", 7);

    if (!player) {
        if (SameName(name, "advanced")) {
            ui.mainRequest = n;
            return true;
        }

        for (int i = 0; i < kTabCount; ++i) {
            if (SameName(name, kTabs[i].name) || (i == int(RsxrUiTab::Colour) && SameName(name, "color"))) {
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

void RsxrOverlayUi_Draw(RsxrOverlayUi& ui, bool* open) {
    const float f = ImGui::GetFontSize();
    RsxrUiPlayerTab tabs[kRsxrUiMaxPlayerTabs];
    const int n = std::min(RsxrUi_PlayerTabs(tabs, kRsxrUiMaxPlayerTabs), kRsxrUiMaxPlayerTabs);

    ImGui::SetNextWindowPos(ImVec2(f * 1.5f, f * 1.5f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(f * 58.0f, DefaultHeight(tabs, n)), ImGuiCond_FirstUseEver);
    char k[16];
    char title[64];

    std::snprintf(title, sizeof title, "RS Extended Range %s  (%s)###rsxroverlay", RSXR_VERSION,
                  KeyName(ui.hotkey, k, sizeof k));

    if (!ImGui::Begin(title, open)) {
        ImGui::End();
        return;
    }

    SaveBar(ui);
    ImGui::TextDisabled("%s", kRestartNote);
    MainTabs(ui, tabs, n);
    ImGui::End();
}
