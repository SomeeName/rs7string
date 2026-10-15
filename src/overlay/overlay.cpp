#include "overlay/overlay.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"
#include "core/overlaycmd.h"
#include "core/settings.h"
#include "d3d/present.h"
#include "imgui.h"
#include "log.h"
#include "overlay/ui.h"
#include <cstdio>
#include <cstring>
#include <d3d9.h>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
wchar_t g_ini[MAX_PATH];
wchar_t g_dir[MAX_PATH];
bool g_enabled;
int g_hotkey = VK_F9;
volatile LONG g_visible;
volatile LONG g_toggles;  // hotkey presses not yet handled by the render thread
DWORD g_renderTid, g_wndTid;
HWND g_hwnd;
WNDPROC g_prevProc;
bool g_wndTried;
bool g_imgui;  // context + both backends initialised
bool g_imguiFailed;
volatile bool g_wantMouse, g_wantKeys;
Rs7OverlayUi g_ui;

// Window messages for ImGui when the window thread is not the render thread (replayed in Present).
struct Msg {
    UINT m;
    WPARAM w;
    LPARAM l;
};

constexpr unsigned kMsgCap = 256;
Msg g_msgs[kMsgCap];
unsigned g_msgCount;
SRWLOCK g_msgLock = SRWLOCK_INIT;

bool IsMouseMsg(UINT m) {
    return (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) || m == WM_MOUSELEAVE;
}

bool IsKeyMsg(UINT m) {
    return m == WM_KEYDOWN || m == WM_KEYUP || m == WM_SYSKEYDOWN || m == WM_SYSKEYUP || m == WM_CHAR;
}

bool ForImGui(UINT m) {
    return IsMouseMsg(m) || IsKeyMsg(m) || m == WM_SETFOCUS || m == WM_KILLFOCUS;
}

void SetVisible(bool on, const char* by) {
    if ((g_visible != 0) == on) {
        return;
    }

    InterlockedExchange(&g_visible, on ? 1 : 0);
    Rs7Log("overlay: %s (%s)", on ? "shown" : "hidden", by);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if ((m == WM_KEYDOWN || m == WM_SYSKEYDOWN) && int(w) == g_hotkey) {
        if (!(l & (1 << 30))) {
            InterlockedIncrement(&g_toggles);  // not an auto-repeat
        }
        return 0;  // the game never sees the hotkey
    }

    if ((m == WM_KEYUP || m == WM_SYSKEYUP) && int(w) == g_hotkey) {
        return 0;
    }

    if (g_visible && g_imgui && ForImGui(m)) {
        if (GetCurrentThreadId() == g_renderTid) {
            ImGui_ImplWin32_WndProcHandler(h, m, w, l);
        } else {
            AcquireSRWLockExclusive(&g_msgLock);
            if (g_msgCount < kMsgCap) {
                g_msgs[g_msgCount++] = {m, w, l};
            }
            ReleaseSRWLockExclusive(&g_msgLock);
        }

        if (IsMouseMsg(m) && m != WM_MOUSEMOVE && g_wantMouse) {
            return 0;
        }
        if (IsKeyMsg(m) && g_wantKeys) {
            return 0;
        }
    }

    return CallWindowProcW(g_prevProc, h, m, w, l);
}

void HookWindow(IDirect3DDevice9* dev) {
    g_wndTried = true;
    D3DDEVICE_CREATION_PARAMETERS cp{};
    HWND h = SUCCEEDED(dev->GetCreationParameters(&cp)) ? cp.hFocusWindow : nullptr;

    if (!h || !IsWindow(h)) {
        Rs7Log("overlay: no focus window in the device's creation parameters - no hotkey, no input");
        return;
    }

    g_hwnd = h;
    g_wndTid = GetWindowThreadProcessId(h, nullptr);
    g_prevProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc)));
    Rs7Log("overlay: window %p subclassed (previous procedure %p%s); window thread %lu, render thread %lu%s",
           static_cast<void*>(h), reinterpret_cast<void*>(g_prevProc),
           GetModuleHandleW(L"xinput1_3.dll") ? ", RSMods present" : "", g_wndTid, g_renderTid,
           g_wndTid == g_renderTid ? " (same)" : " (different: messages are replayed on the render thread)");
    if (!g_prevProc) {
        Rs7Log("overlay: SetWindowLongPtrW failed (%lu) - no hotkey, no input", GetLastError());
    }
}

bool InitImGui(IDirect3DDevice9* dev) {
    if (g_imguiFailed) {
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    // Never touch the game's cursor shape (it would be set from the render thread); our own cursor
    // is drawn while the game window is focused.
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::StyleColorsDark();
    const bool ok = g_hwnd && ImGui_ImplWin32_Init(g_hwnd) && ImGui_ImplDX9_Init(dev);

    if (!ok) {
        g_imguiFailed = true;
        Rs7Log("overlay: ImGui init failed (window %p) - overlay off", static_cast<void*>(g_hwnd));
        return false;
    }

    g_imgui = true;
    Rs7Log("overlay: Dear ImGui %s ready (own context, no imgui.ini)", IMGUI_VERSION);

    return true;
}

void ApplyScale() {
    float s = Rs7Set_Float("overlay.scale");

    if (s <= 0.0f) {
        RECT r{};

        GetClientRect(g_hwnd, &r);
        s = float(r.bottom - r.top) / 720.0f;
        s = s < 1.0f ? 1.0f : s > 3.0f ? 3.0f : s;
    }

    if (s == g_ui.appliedScale) {
        return;
    }

    ImGuiStyle& st = ImGui::GetStyle();

    st = ImGuiStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 4.0f;
    st.Colors[ImGuiCol_WindowBg].w = 0.93f;
    st.ScaleAllSizes(s);
    st.FontScaleMain = s;
    g_ui.appliedScale = s;
}

void RunCommand(const Rs7Cmd& c) {
    switch (c.kind) {
        case Rs7CmdKind::Toggle:
            SetVisible(!g_visible, "pipe");
            Rs7Log("overlay: cmd toggle -> ok");
            break;
        case Rs7CmdKind::Show:
            SetVisible(true, "pipe");
            Rs7Log("overlay: cmd show -> ok");
            break;
        case Rs7CmdKind::Hide:
            SetVisible(false, "pipe");
            Rs7Log("overlay: cmd hide -> ok");
            break;
        case Rs7CmdKind::Tab: {
            const int t = Rs7OverlayUi_TabIndex(c.key.c_str());

            if (t >= 0) {
                g_ui.tabRequest = t;
            }
            Rs7Log("overlay: cmd tab %s -> %s", c.key.c_str(), t >= 0 ? "ok" : "unknown tab");
            break;
        }
        case Rs7CmdKind::Set: {
            const Rs7SetResult r = Rs7Set_SetText(c.key.c_str(), c.value.c_str());
            const int i = Rs7Set_Find(c.key.c_str());

            Rs7Log("overlay: cmd set %s %s -> %s%s%s", c.key.c_str(), c.value.c_str(), Rs7Set_ResultName(r),
                   i >= 0 ? ", now " : "", i >= 0 ? Rs7Set_Format(i, Rs7Set_State(i).value).c_str() : "");
            break;
        }
        case Rs7CmdKind::Save: {
            const int n = Rs7Set_Save(g_ini);

            Rs7Log("overlay: cmd save -> %d keys written to %ls", n, g_ini);
            break;
        }
        case Rs7CmdKind::Status:
            Rs7Log("overlay: cmd status -> visible=%d frames=%lu faults=%lu imgui=%d dirty=%d window=%p",
                   int(g_visible), Rs7Present_Frames(), Rs7Present_Faults(), int(g_imgui), Rs7Set_DirtyCount(),
                   static_cast<void*>(g_hwnd));
            break;
    }
}

void ReplayMessages() {
    Msg local[kMsgCap];
    unsigned n = 0;

    AcquireSRWLockExclusive(&g_msgLock);
    n = g_msgCount;
    std::memcpy(local, g_msgs, n * sizeof(Msg));
    g_msgCount = 0;
    ReleaseSRWLockExclusive(&g_msgLock);

    for (unsigned i = 0; i < n; ++i) {
        ImGui_ImplWin32_WndProcHandler(g_hwnd, local[i].m, local[i].w, local[i].l);
    }
}

// Draws the frame onto the back buffer in its own scene, then puts back what the game had bound.
void Draw(IDirect3DDevice9* dev) {
    ApplyScale();
    ReplayMessages();
    ImGuiIO& io = ImGui::GetIO();

    io.MouseDrawCursor = GetForegroundWindow() == g_hwnd;
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    g_ui.iniPath = g_ini;
    g_ui.gameDir = g_dir;
    g_ui.hotkey = g_hotkey;
    g_ui.hwnd = g_hwnd;
    g_ui.renderTid = g_renderTid;
    g_ui.wndTid = g_wndTid;
    bool open = true;

    Rs7OverlayUi_Draw(g_ui, &open);
    if (!open) {
        SetVisible(false, "close button");
    }
    ImGui::EndFrame();
    ImGui::Render();
    g_wantMouse = io.WantCaptureMouse;
    g_wantKeys = io.WantCaptureKeyboard;

    IDirect3DSurface9* back = nullptr;
    IDirect3DSurface9* rt = nullptr;
    IDirect3DSurface9* ds = nullptr;
    D3DVIEWPORT9 vp{};

    dev->GetViewport(&vp);
    dev->GetRenderTarget(0, &rt);
    dev->GetDepthStencilSurface(&ds);  // ImGui needs no depth; a smaller one would make the draw invalid
    const bool other = SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) && back && back != rt;

    if (other) {
        dev->SetRenderTarget(0, back);
    }
    if (ds) {
        dev->SetDepthStencilSurface(nullptr);
    }

    if (SUCCEEDED(dev->BeginScene())) {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        dev->EndScene();  // RSMods' EndScene hook sees a non-game caller and leaves it alone
    }

    if (ds) {
        dev->SetDepthStencilSurface(ds);
    }
    if (other && rt) {
        dev->SetRenderTarget(0, rt);
    }
    dev->SetViewport(&vp);  // SetRenderTarget resets the viewport; the state block restored the rest
    if (back) {
        back->Release();
    }
    if (rt) {
        rt->Release();
    }
    if (ds) {
        ds->Release();
    }
    // The backend restores the game's state with a state block, which bypasses SetStreamSource.
    // RSMods mirrors stream 0's stride from its SetStreamSource hook: tell it what is bound again.
    IDirect3DVertexBuffer9* vb = nullptr;
    UINT off = 0, stride = 0;

    if (SUCCEEDED(dev->GetStreamSource(0, &vb, &off, &stride))) {
        dev->SetStreamSource(0, vb, off, stride);
        if (vb) {
            vb->Release();
        }
    }
}

void OnPresent(IDirect3DDevice9* dev) {
    if (!g_renderTid) {
        g_renderTid = GetCurrentThreadId();
        Rs7Log("overlay: first Present on thread %lu (hotkey %s = VK %02X)", g_renderTid, Rs7Set_Text("overlay.hotkey"),
               unsigned(g_hotkey));
    }

    if (!g_wndTried) {
        HookWindow(dev);
    }

    for (LONG t = InterlockedExchange(&g_toggles, 0); t > 0; --t) {
        SetVisible(!g_visible, "hotkey");
    }

    Rs7Cmd c;

    while (Rs7Cmd_Pop(&c)) {
        RunCommand(c);
    }

    if (!g_visible) {
        return;
    }
    if (dev->TestCooperativeLevel() != D3D_OK) {
        return;  // lost: the game resets it first
    }
    if (!g_imgui && !InitImGui(dev)) {
        return;
    }

    Draw(dev);
}

void BeforeReset(IDirect3DDevice9*) {
    if (g_imgui) {
        ImGui_ImplDX9_InvalidateDeviceObjects();  // DEFAULT-pool buffers/textures: Reset needs them gone
    }
}

void AfterReset(IDirect3DDevice9*, HRESULT hr) {
    Rs7Log("overlay: device Reset -> %08lX (ImGui objects are recreated at the next frame)", hr);
}
}  // namespace

void Rs7Overlay_Configure(const wchar_t* iniPath) {
    wcsncpy_s(g_ini, iniPath, _TRUNCATE);
    wcsncpy_s(g_dir, iniPath, _TRUNCATE);
    if (wchar_t* slash = wcsrchr(g_dir, L'\\')) {
        slash[1] = 0;
    }
    g_enabled = Rs7Set_Bool("overlay.enabled");
    const int vk = Rs7_ParseHotkey(Rs7Set_Text("overlay.hotkey"));

    g_hotkey = vk ? vk : VK_F9;
    if (Rs7Set_Bool("overlay.open")) {
        InterlockedExchange(&g_visible, 1);
    }
    Rs7Log("config: [overlay] enabled=%d hotkey=%s%s open=%d scale=%.2f -> %s", int(g_enabled),
           Rs7Set_Text("overlay.hotkey"), vk ? "" : " (INVALID, F9 used)", int(Rs7Set_Bool("overlay.open")),
           double(Rs7Set_Float("overlay.scale")),
           g_enabled ? "in-game overlay (Present/Reset hooks once the device is found)" : "off (no hooks)");
}

bool Rs7Overlay_Enabled() {
    return g_enabled;
}

void Rs7Overlay_OnDevice(void* device) {
    Rs7PresentCallbacks cb{};

    cb.onPresent = OnPresent;
    cb.beforeReset = BeforeReset;
    cb.afterReset = AfterReset;
    if (!Rs7Present_InstallOn(device, cb)) {
        Rs7Log("overlay: not installed");
    }
}
