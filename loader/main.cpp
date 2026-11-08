#include "app/app.h"
#include "gfx/device.h"
#include "gfx/renderer.h"
#include "launcher.hpp"
#include "ui/ui.h"

#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace {

constexpr wchar_t kWindowClass[] = L"PastaLoaderWindow";
constexpr wchar_t kInstanceMutex[] = L"Local\\PastaLoader.SingleInstance";
constexpr int kMaxPanelWidth = 372;
constexpr int kMaxPanelHeight = 236;
constexpr int kMargin = 26;
constexpr int kWindowWidth = kMaxPanelWidth + kMargin * 2;
constexpr int kWindowHeight = kMaxPanelHeight + kMargin * 2;

enum class load_phase {
    idle,
    updating,
    running,
    success,
    failure,
};

std::atomic<bool> g_cancel = false;
std::mutex g_state_mutex;
load_phase g_phase = load_phase::idle;
std::wstring g_status;
std::wstring g_detail;
int g_progress = 0;
bool g_cancelling = false;
bool g_update_failed = false;
std::thread g_worker;

struct WindowState {
    HWND window = nullptr;
    app::App app;
    gfx::Device device;
    gfx::Renderer renderer;
    ui::Input input;
    bool mouseDown = false;
    bool mousePressed = false;
    bool mouseReleased = false;
    bool trackingMouse = false;
    bool dragging = false;
    POINT dragCursor = {};
    RECT dragWindow = {};
    bool running = true;
    float scale = 1.f;
    bool headless = false;
    bool wantShot = false;
    std::wstring shotPath;
    float shotAfter = 1.f;
    float elapsed = 0.f;
};

struct InstanceMutex {
    HANDLE handle = nullptr;
    ~InstanceMutex() {
        if (handle) CloseHandle(handle);
    }
};

WindowState* g_state = nullptr;

void set_status(const int progress, std::wstring status, std::wstring detail) {
    const std::scoped_lock lock(g_state_mutex);
    if (g_phase != load_phase::running && g_phase != load_phase::updating) return;
    g_status = std::move(status);
    g_detail = std::move(detail);
    g_progress = std::clamp(progress, 0, 100);
}

app::ViewState snapshot() {
    app::ViewState view;
    const std::scoped_lock lock(g_state_mutex);
    switch (g_phase) {
    case load_phase::idle: view.phase = app::Phase::idle; break;
    case load_phase::updating: view.phase = app::Phase::downloading; break;
    case load_phase::running: view.phase = app::Phase::running; break;
    case load_phase::success: view.phase = app::Phase::success; break;
    case load_phase::failure: view.phase = app::Phase::failure; break;
    }
    view.status = g_status;
    view.detail = g_detail;
    view.progress = g_progress;
    view.cancelling = g_cancelling;
    return view;
}

void worker_main() {
    const auto failure = loader::launch(g_cancel, set_status);

    const std::scoped_lock lock(g_state_mutex);
    if (g_cancel.load(std::memory_order_relaxed)) {
        g_phase = load_phase::idle;
        g_status = L"Load cancelled";
        g_detail = L"Nothing was started.";
        g_progress = 0;
        g_cancelling = false;
        return;
    }

    g_cancelling = false;
    if (failure) {
        g_phase = load_phase::failure;
        g_status = failure->status;
        g_detail = failure->detail;
        g_progress = 0;
    } else {
        g_phase = load_phase::success;
        g_status = L"PASTA is running";
        g_detail = L"Launch complete. Closing automatically.";
        g_progress = 100;
    }
}

void update_worker() {
    const auto failure = loader::prepare(g_cancel, set_status);

    const std::scoped_lock lock(g_state_mutex);
    if (g_cancel.load(std::memory_order_relaxed)) {
        g_phase = load_phase::failure;
        g_status = L"Update cancelled";
        g_detail = L"PASTA was not updated. Try again when ready.";
        g_progress = 0;
        g_cancelling = false;
        g_update_failed = true;
        return;
    }

    g_cancelling = false;
    if (failure) {
        g_phase = load_phase::failure;
        g_status = failure->status;
        g_detail = failure->detail;
        g_progress = 0;
        g_update_failed = true;
    } else {
        g_phase = load_phase::idle;
        g_status.clear();
        g_detail.clear();
        g_progress = 0;
        g_update_failed = false;
    }
}

void start_update_check() {
    {
        const std::scoped_lock lock(g_state_mutex);
        if (g_phase == load_phase::updating || g_phase == load_phase::running) return;
    }
    if (g_worker.joinable()) g_worker.join();

    g_cancel.store(false, std::memory_order_relaxed);
    {
        const std::scoped_lock lock(g_state_mutex);
        g_phase = load_phase::updating;
        g_status = L"Checking for updates";
        g_detail = L"Contacting the PASTA release feed.";
        g_progress = 0;
        g_cancelling = false;
        g_update_failed = false;
    }
    g_worker = std::thread(update_worker);
}

void start_loading() {
    {
        const std::scoped_lock lock(g_state_mutex);
        if (g_phase == load_phase::updating || g_phase == load_phase::running) return;
    }
    if (g_worker.joinable()) g_worker.join();

    g_cancel.store(false, std::memory_order_relaxed);
    {
        const std::scoped_lock lock(g_state_mutex);
        g_phase = load_phase::running;
        g_status = L"Starting PASTA";
        g_detail = L"Checking Steam and Counter-Strike 2.";
        g_progress = 0;
        g_cancelling = false;
        g_update_failed = false;
    }
    g_worker = std::thread(worker_main);
}

void request_cancel() {
    g_cancel.store(true, std::memory_order_relaxed);
    const std::scoped_lock lock(g_state_mutex);
    if (g_phase == load_phase::running || g_phase == load_phase::updating) {
        g_cancelling = true;
        g_status = g_phase == load_phase::updating ? L"Cancelling update" : L"Cancelling";
        g_detail = L"Stopping the current step.";
    }
}

void update_mouse(HWND window, const LPARAM lparam) {
    if (!g_state) return;
    const POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
    const float scale = g_state->scale > 0.f ? g_state->scale : 1.f;
    g_state->input.mouse = core::Vec2((float)point.x / scale, (float)point.y / scale);

    if (!g_state->trackingMouse) {
        TRACKMOUSEEVENT track = {};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = window;
        if (TrackMouseEvent(&track)) g_state->trackingMouse = true;
    }
}

LRESULT CALLBACK window_proc(const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam) {
    switch (message) {
    case WM_CLOSE:
        if (g_state) g_state->app.requestClose();
        return 0;

    case WM_DESTROY:
        if (g_state) g_state->running = false;
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE && g_state) g_state->app.requestClose();
        return 0;

    case WM_LBUTTONDOWN:
        if (!g_state) return 0;
        SetCapture(window);
        g_state->mouseDown = true;
        g_state->mousePressed = true;
        update_mouse(window, lparam);
        {
            const core::Rect panel = g_state->app.panelRect();
            const float scale = g_state->scale > 0.f ? g_state->scale : 1.f;
            const float x = (float)GET_X_LPARAM(lparam) / scale;
            const float y = (float)GET_Y_LPARAM(lparam) / scale;
            if (panel.w > 0.f && y >= panel.y && y < panel.y + app::kTitleBarHeight && x < panel.r() - 38.f) {
                g_state->dragging = true;
                GetCursorPos(&g_state->dragCursor);
                GetWindowRect(window, &g_state->dragWindow);
                SetCapture(window);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (!g_state) return 0;
        g_state->mouseDown = false;
        g_state->mouseReleased = true;
        g_state->dragging = false;
        ReleaseCapture();
        update_mouse(window, lparam);
        return 0;

    case WM_CAPTURECHANGED:
        if (g_state) {
            g_state->dragging = false;
            g_state->mouseDown = false;
        }
        return 0;

    case WM_MOUSEMOVE:
        if (!g_state) return 0;
        update_mouse(window, lparam);
        if (g_state->dragging && (wparam & MK_LBUTTON)) {
            POINT cursor = {};
            GetCursorPos(&cursor);
            const int dx = cursor.x - g_state->dragCursor.x;
            const int dy = cursor.y - g_state->dragCursor.y;
            SetWindowPos(window, nullptr, g_state->dragWindow.left + dx, g_state->dragWindow.top + dy, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;

    case WM_MOUSELEAVE:
        if (g_state) {
            g_state->trackingMouse = false;
            g_state->input.mouse = core::Vec2(-1.f, -1.f);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool parse_command_line(std::wstring& shotPath, bool& headless, float& shotAfter) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;

    bool ok = true;
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"-shot") == 0 && i + 1 < argc) {
            shotPath = argv[++i];
            headless = true;
        } else if (std::wcscmp(argv[i], L"-t") == 0 && i + 1 < argc) {
            wchar_t* end = nullptr;
            const double value = std::wcstod(argv[++i], &end);
            if (end && end != argv[i] && value > 0.0) shotAfter = (float)value;
        }
    }

    LocalFree(argv);
    return ok;
}

void focus_existing_loader() {
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (const HWND existing = FindWindowW(kWindowClass, nullptr)) {
            ShowWindow(existing, SW_RESTORE);
            SetWindowPos(existing, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            SetForegroundWindow(existing);
            return;
        }
        Sleep(50);
    }
}

} // namespace

int WINAPI wWinMain(const HINSTANCE instance, HINSTANCE, PWSTR, const int showCommand) {
    InstanceMutex instanceMutex;
    SetLastError(ERROR_SUCCESS);
    instanceMutex.handle = CreateMutexW(nullptr, FALSE, kInstanceMutex);
    if (instanceMutex.handle && GetLastError() == ERROR_ALREADY_EXISTS) {
        focus_existing_loader();
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) {
        MessageBoxW(nullptr, L"Could not initialize COM.", L"PASTA", MB_ICONERROR | MB_OK);
        return 1;
    }

    WindowState state;
    g_state = &state;

    std::wstring shotPath;
    parse_command_line(shotPath, state.headless, state.shotAfter);
    state.wantShot = state.headless && !shotPath.empty();
    state.shotPath = shotPath;

    const HINSTANCE module = instance ? instance : GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = window_proc;
    windowClass.hInstance = module;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) {
        g_state = nullptr;
        CoUninitialize();
        return 1;
    }

    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int windowWidth = kWindowWidth;
    int windowHeight = kWindowHeight;
    const int x = work.left + (work.right - work.left - windowWidth) / 2;
    const int y = work.top + (work.bottom - work.top - windowHeight) / 2;

    state.window = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW, kWindowClass, L"PASTA", WS_POPUP, x,
                                   y, windowWidth, windowHeight, nullptr, nullptr, module, nullptr);
    if (!state.window) {
        g_state = nullptr;
        UnregisterClassW(kWindowClass, module);
        CoUninitialize();
        return 1;
    }

    // GetDpiForSystem can still report 96 while the created window is already on a 150% monitor. Use the window's DPI
    // and resize the physical surface around the fixed 96-DPI design used by the transplanted UI.
    const UINT windowDpi = GetDpiForWindow(state.window);
    state.scale = (std::max)(1.f, (float)windowDpi / 96.f);
    windowWidth = (int)std::lround((float)kWindowWidth * state.scale);
    windowHeight = (int)std::lround((float)kWindowHeight * state.scale);
    if (windowWidth != kWindowWidth || windowHeight != kWindowHeight) {
        const int scaledX = work.left + (work.right - work.left - windowWidth) / 2;
        const int scaledY = work.top + (work.bottom - work.top - windowHeight) / 2;
        SetWindowPos(state.window, nullptr, scaledX, scaledY, windowWidth, windowHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (!state.device.create(state.window, kWindowWidth, kWindowHeight, state.scale) ||
        !state.renderer.create(state.device.dev()) || !state.app.init(state.device.dev())) {
        MessageBoxW(state.window, L"Could not initialize the PASTA interface.", L"PASTA", MB_ICONERROR | MB_OK);
        state.app.shutdown();
        state.renderer.destroy();
        state.device.destroy();
        DestroyWindow(state.window);
        g_state = nullptr;
        UnregisterClassW(kWindowClass, module);
        CoUninitialize();
        return 1;
    }

    if (!state.headless) {
        ShowWindow(state.window, showCommand == SW_SHOWMINIMIZED ? SW_SHOWNORMAL : showCommand);
        UpdateWindow(state.window);
        SetForegroundWindow(state.window);
        start_update_check();
    }

    LARGE_INTEGER frequency = {};
    LARGE_INTEGER previous = {};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);

    while (state.running) {
        MSG message = {};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                state.running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!state.running) break;

        LARGE_INTEGER now = {};
        QueryPerformanceCounter(&now);
        float dt = 0.f;
        if (frequency.QuadPart > 0) {
            dt = (float)((double)(now.QuadPart - previous.QuadPart) / (double)frequency.QuadPart);
        }
        previous = now;
        dt = std::clamp(dt, 0.f, 0.05f);
        state.elapsed += dt;

        ui::Input input;
        input.mouse = state.input.mouse;
        input.down = state.mouseDown;
        input.pressed = state.mousePressed;
        input.released = state.mouseReleased;
        state.mousePressed = false;
        state.mouseReleased = false;

        ui::newFrame(input, dt, (float)kWindowWidth, (float)kWindowHeight);
        const app::ViewState view = snapshot();
        const app::Actions actions = state.app.frame(dt, view);
        ui::endFrame();

        if (actions.load) {
            bool retry_update = false;
            {
                const std::scoped_lock lock(g_state_mutex);
                retry_update = g_update_failed;
            }
            if (retry_update) start_update_check();
            else start_loading();
        }
        if (actions.cancel) request_cancel();
        if (state.app.closeRequested()) g_cancel.store(true, std::memory_order_relaxed);

        state.device.beginFrame();
        state.renderer.render(state.device.ctx(), ui::dl(), kWindowWidth, kWindowHeight, state.app.fadeAlpha());
        state.device.present(true);

        if (state.wantShot && state.elapsed >= state.shotAfter) {
            state.device.captureBackbuffer(state.shotPath);
            state.wantShot = false;
            state.running = false;
        }
        if (state.app.finished()) state.running = false;
    }

    g_cancel.store(true, std::memory_order_relaxed);
    if (g_worker.joinable()) g_worker.join();

    state.app.shutdown();
    state.renderer.destroy();
    state.device.destroy();
    if (IsWindow(state.window)) DestroyWindow(state.window);
    g_state = nullptr;
    UnregisterClassW(kWindowClass, module);
    CoUninitialize();
    return 0;
}
