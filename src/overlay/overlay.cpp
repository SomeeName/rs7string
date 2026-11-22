#include "overlay/overlay.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"
#include "d3d/present.h"
#include "features/arrangements/arrangements.h"
#include "features/testdrive/testdrive.h"
#include "imgui.h"
#include "overlay/capture.h"
#include "overlay/overlaycmd.h"
#include "overlay/ui.h"
#include "platform/config.h"
#include "platform/log.h"
#include "platform/settings.h"
#include <cstring>
#include <d3d9.h>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
wchar_t g_ini[MAX_PATH];
bool g_enabled;
int g_hotkey = VK_F9;
volatile LONG g_visible;
volatile LONG g_faulted;  // sticky: the callback guard disabled the overlay
volatile LONG g_toggles;  // hotkey presses not yet handled by the render thread
DWORD g_renderTid, g_wndTid;
HWND g_hwnd;
WNDPROC g_prevProc;
bool g_wndTried;
bool g_imgui;  // context + both backends initialised
bool g_imguiFailed;
RsxrOverlayUi g_ui;

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
    const LONG want = on ? 1 : 0;

    if (InterlockedExchange(&g_visible, want) == want) {
        return;
    }

    if (!on) {
        RsxrOverlay_SetCapture(false, false);  // hidden: the game gets all input again
        // Every caller runs on the render thread (OnPresent and what it calls), so ImGui may be touched here.
        // Drop input queued while shown and release held keys/buttons: their up messages never reach ImGui
        // while hidden, and nothing stale is replayed on the next show.
        AcquireSRWLockExclusive(&g_msgLock);
        g_msgCount = 0;
        ReleaseSRWLockExclusive(&g_msgLock);

        if (g_imgui) {
            ImGui::GetIO().ClearEventsQueue();
            ImGui::GetIO().ClearInputKeys();
            ImGui::GetIO().ClearInputMouse();
        }
    }

    RsxrLog("overlay: %s (%s)", on ? "shown" : "hidden", by);
}

// Window thread only: the hotkey's WM_KEYDOWN was swallowed, so swallow its WM_CHAR too. TranslateMessage posts that
// WM_CHAR before the key's WM_KEYUP; a hotkey without one (an F key) is cleared at its key-up, so a later WM_CHAR
// without a key-down (IME, Alt codes) gets through.
bool g_hotkeyChar;

LRESULT PassToGame(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN && !(l & (1 << 30))) {
        RsxrArr_KeyDown(unsigned(w));  // only keys the overlay passed through may page arrangements
    }

    return CallWindowProcW(g_prevProc, h, m, w, l);
}

void OnFault() {
    InterlockedExchange(&g_faulted, 1);
    InterlockedExchange(&g_visible, 0);
    RsxrOverlay_SetCapture(false, false);
    // No ImGui calls or message-queue lock: the fault may have left that state incomplete.
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_faulted) {
        return PassToGame(h, m, w, l);
    }

    if (m == WM_CHAR || m == WM_SYSCHAR) {
        if (g_hotkeyChar) {
            g_hotkeyChar = false;
            return 0;
        }
    } else if (m == WM_KEYDOWN || m == WM_SYSKEYDOWN) {
        g_hotkeyChar = int(w) == g_hotkey;
    } else if (m == WM_KEYUP || m == WM_SYSKEYUP) {
        g_hotkeyChar = false;
    }

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
            if (g_visible && !g_faulted && g_msgCount < kMsgCap) {
                g_msgs[g_msgCount++] = {m, w, l};
            }
            ReleaseSRWLockExclusive(&g_msgLock);
        }

        if (IsMouseMsg(m) && m != WM_MOUSEMOVE && RsxrOverlay_WantsMouse()) {
            return 0;
        }
        if (IsKeyMsg(m) && RsxrOverlay_WantsKeys()) {
            return 0;
        }
    }

    return PassToGame(h, m, w, l);
}

void HookWindow(IDirect3DDevice9* dev) {
    g_wndTried = true;
    D3DDEVICE_CREATION_PARAMETERS cp{};
    HWND h = SUCCEEDED(dev->GetCreationParameters(&cp)) ? cp.hFocusWindow : nullptr;

    if (!h || !IsWindow(h)) {
        RsxrLog("overlay: no focus window in the device's creation parameters - no hotkey, no input");
        return;
    }

    g_hwnd = h;
    g_wndTid = GetWindowThreadProcessId(h, nullptr);
    // g_prevProc must be valid before WndProc is installed: the window thread may call it at once.
    g_prevProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(h, GWLP_WNDPROC));
    const auto prev =
        reinterpret_cast<WNDPROC>(SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc)));
    const DWORD err = prev ? 0 : GetLastError();  // before the log line below can change it

    if (prev) {
        g_prevProc = prev;  // someone subclassed in between
    }
    RsxrLog("overlay: window %p subclassed (previous procedure %p%s); window thread %lu, render thread %lu%s",
            static_cast<void*>(h), reinterpret_cast<void*>(g_prevProc),
            GetModuleHandleW(L"xinput1_3.dll") ? ", RSMods present" : "", g_wndTid, g_renderTid,
            g_wndTid == g_renderTid ? " (same)" : " (different: messages are replayed on the render thread)");
    if (!prev) {
        RsxrLog("overlay: SetWindowLongPtrW failed (%lu) - no hotkey, no input", err);
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
        RsxrLog("overlay: ImGui init failed (window %p) - overlay off", static_cast<void*>(g_hwnd));
        return false;
    }

    g_imgui = true;
    // Fixed from here on: Configure, the first Present and HookWindow all ran before this.
    g_ui.iniPath = g_ini;
    g_ui.hotkey = g_hotkey;
    g_ui.hwnd = g_hwnd;
    g_ui.renderTid = g_renderTid;
    g_ui.wndTid = g_wndTid;
    RsxrLog("overlay: Dear ImGui %s ready (own context, no imgui.ini)", IMGUI_VERSION);

    return true;
}

void ApplyScale() {
    float s = RsxrSet_Float("overlay.scale");

    if (s > 0.0f && s < 0.5f) {
        s = 0.5f;  // dragging the slider off "Automatic" must not make the menu unreadable
    }

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

void RunCommand(const RsxrCmd& c) {
    switch (c.kind) {
        case RsxrCmdKind::Toggle:
            SetVisible(!g_visible, "pipe");
            RsxrLog("overlay: cmd toggle -> ok");
            break;
        case RsxrCmdKind::Show:
            SetVisible(true, "pipe");
            RsxrLog("overlay: cmd show -> ok");
            break;
        case RsxrCmdKind::Hide:
            SetVisible(false, "pipe");
            RsxrLog("overlay: cmd hide -> ok");
            break;
        case RsxrCmdKind::Tab: {
            const bool ok = RsxrOverlayUi_RequestTab(g_ui, c.key.c_str());

            RsxrLog("overlay: cmd tab %s -> %s", c.key.c_str(), ok ? "ok" : "unknown tab");
            break;
        }
        case RsxrCmdKind::Set: {
            const RsxrSetResult r = RsxrSet_SetText(c.key.c_str(), c.value.c_str());
            const int i = RsxrSet_Find(c.key.c_str());

            RsxrLog("overlay: cmd set %s %s -> %s%s%s", c.key.c_str(), c.value.c_str(), RsxrSet_ResultName(r),
                    i >= 0 ? ", now " : "", i >= 0 ? RsxrSet_Format(i, RsxrSet_State(i).value).c_str() : "");
            break;
        }
        case RsxrCmdKind::Save: {
            const int n = RsxrSet_Save(g_ini);

            if (n < 0) {
                RsxrLog("overlay: cmd save -> write error, %ls not fully saved", g_ini);
            } else {
                RsxrLog("overlay: cmd save -> %d keys written to %ls", n, g_ini);
            }
            break;
        }
        case RsxrCmdKind::Status:
            RsxrLog("overlay: cmd status -> visible=%d frames=%lu faults=%lu imgui=%d dirty=%d window=%p",
                    int(g_visible), RsxrPresent_Frames(), RsxrPresent_Faults(), int(g_imgui), RsxrSet_DirtyCount(),
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

    // Known limit: the backend reads Ctrl/Shift/Alt with GetKeyState on this thread, not from the message.
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
    bool open = true;

    RsxrOverlayUi_Draw(g_ui, &open);
    if (!open) {
        SetVisible(false, "close button");
    }
    ImGui::EndFrame();
    ImGui::Render();
    RsxrOverlay_SetCapture(g_visible && io.WantCaptureMouse, g_visible && io.WantCaptureKeyboard);

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
    RsxrVariant_OnFrame();  // [test] force_colourblind: after the frame's draws, before the next update (testdrive.h)

    if (!g_renderTid) {
        g_renderTid = GetCurrentThreadId();
        RsxrLog("overlay: first Present on thread %lu (hotkey %s = VK %02X)", g_renderTid,
                RsxrSet_Text("overlay.hotkey"), unsigned(g_hotkey));
    }

    if (!g_wndTried) {
        HookWindow(dev);
    }

    for (LONG t = InterlockedExchange(&g_toggles, 0); t > 0; --t) {
        SetVisible(!g_visible, "hotkey");
    }

    RsxrCmd c;

    while (RsxrCmd_Pop(&c)) {
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
    RsxrLog("overlay: device Reset -> %08lX (ImGui objects are recreated at the next frame)", hr);
}
}  // namespace

void RsxrOverlay_Configure(const wchar_t* iniPath) {
    wcsncpy_s(g_ini, iniPath, _TRUNCATE);
    g_enabled = RsxrSet_Bool("overlay.enabled");
    const int vk = Rsxr_ParseHotkey(RsxrSet_Text("overlay.hotkey"));

    g_hotkey = vk ? vk : VK_F9;
    if (RsxrSet_Bool("overlay.open")) {
        InterlockedExchange(&g_visible, 1);
    }
    RsxrLog("config: [overlay] enabled=%d hotkey=%s%s open=%d scale=%.2f -> %s", int(g_enabled),
            RsxrSet_Text("overlay.hotkey"), vk ? "" : " (INVALID, F9 used)", int(RsxrSet_Bool("overlay.open")),
            double(RsxrSet_Float("overlay.scale")),
            g_enabled ? "in-game overlay (Present/Reset hooks once the device is found)" : "off (no hooks)");
}

bool RsxrOverlay_Enabled() {
    return g_enabled;
}

void RsxrOverlay_OnDevice(void* device) {
    RsxrPresentCallbacks cb{};

    cb.onPresent = OnPresent;
    cb.beforeReset = BeforeReset;
    cb.afterReset = AfterReset;
    cb.onFault = OnFault;
    if (!RsxrPresent_InstallOn(device, cb)) {
        RsxrLog("overlay: not installed");
    }
}
