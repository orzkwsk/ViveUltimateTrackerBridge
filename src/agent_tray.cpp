#include "vut/agent_tray.hpp"
#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <thread>
#include <string>
#include <cwchar>
namespace vut {
struct AgentTray::Native {
    std::atomic<HWND> window{nullptr};
    std::atomic_bool stop{false}, open{false}, exit{false};
    std::atomic<AgentTrayState> state{AgentTrayState::Starting};
    std::atomic<ULONGLONG> heartbeat{GetTickCount64()};
    std::thread worker;
    UINT recreated = 0;
    NOTIFYICONDATAW icon{};
    bool registered = false;
    static constexpr UINT callback = WM_APP + 41;
    Native() { worker = std::thread([this] { try { loop(); } catch (...) { state = AgentTrayState::Failed; } }); }
    ~Native() {
        stop = true;
        if (const auto hwnd = window.load()) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        if (worker.joinable()) {
            if (WaitForSingleObject(worker.native_handle(), 5000) != WAIT_OBJECT_0) std::terminate();
            worker.join();
        }
    }
    bool addIcon() noexcept {
        registered = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
        state = registered ? AgentTrayState::Visible : AgentTrayState::Failed;
        return registered;
    }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Native*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Native*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, w, l);
        if (message == WM_TIMER && w == 1) { self->heartbeat = GetTickCount64(); return 0; }
        if (self->recreated && message == self->recreated) { self->registered = false; self->addIcon(); return 0; }
        if (message == callback) {
            if (l == WM_LBUTTONDBLCLK) self->open = true;
            if (l == WM_RBUTTONUP || l == WM_CONTEXTMENU) {
                const auto menu = CreatePopupMenu();
                if (!menu) { self->state = AgentTrayState::Failed; return 0; }
                AppendMenuW(menu, MF_STRING, 1, L"Open Ultimate Tracker Receiver");
                AppendMenuW(menu, MF_STRING, 2, L"Exit Agent and tracking");
                POINT point{}; GetCursorPos(&point); SetForegroundWindow(hwnd);
                const auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr);
                DestroyMenu(menu); PostMessageW(hwnd, WM_NULL, 0, 0);
                if (command == 1) self->open = true;
                if (command == 2) self->exit = true;
            }
            return 0;
        }
        if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
        if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
        return DefWindowProcW(hwnd, message, w, l);
    }
    void loop() {
        const auto module = GetModuleHandleW(nullptr);
        const auto className = L"Monaka.Altra.Tray." + std::to_wstring(GetCurrentThreadId());
        WNDCLASSW definition{}; definition.lpfnWndProc = procedure; definition.hInstance = module; definition.lpszClassName = className.c_str();
        if (!RegisterClassW(&definition)) { state = AgentTrayState::Failed; return; }
        recreated = RegisterWindowMessageW(L"TaskbarCreated");
        const auto hwnd = CreateWindowExW(0, className.c_str(), L"Ultimate Tracker Receiver", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, module, this);
        if (!hwnd || !recreated) {
            if (hwnd) DestroyWindow(hwnd);
            UnregisterClassW(className.c_str(), module); state = AgentTrayState::Failed; return;
        }
        window = hwnd;
        if (!SetTimer(hwnd, 1, 500, nullptr)) { state = AgentTrayState::Failed; stop = true; }
        icon.cbSize = sizeof(icon); icon.hWnd = hwnd; icon.uID = 1;
        icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; icon.uCallbackMessage = callback;
        icon.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512)); wcscpy_s(icon.szTip, L"Ultimate Tracker Receiver");
        if (!stop && (!icon.hIcon || !addIcon())) stop = true;
        if (!stop.load()) {
            MSG message{}; int result;
            while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
            if (result < 0) state = AgentTrayState::Failed;
        }
        KillTimer(hwnd, 1);
        if (registered) Shell_NotifyIconW(NIM_DELETE, &icon);
        window = nullptr;
        if (IsWindow(hwnd)) DestroyWindow(hwnd);
        UnregisterClassW(className.c_str(), module);
        if (state != AgentTrayState::Failed) state = AgentTrayState::Stopped;
    }
};
AgentTray::AgentTray() : native_(std::make_unique<Native>()) {}
AgentTray::~AgentTray() = default;
AgentTrayAction AgentTray::takeAction() noexcept {
    if (native_->exit.exchange(false)) { native_->open = false; return AgentTrayAction::Exit; }
    return native_->open.exchange(false) ? AgentTrayAction::Open : AgentTrayAction::None;
}
AgentTrayState AgentTray::state() const noexcept { return native_->state.load(); }
bool AgentTray::responsive() const noexcept {
    const auto s = state();
    return s != AgentTrayState::Failed && s != AgentTrayState::Stopped && GetTickCount64() - native_->heartbeat.load() <= 5000;
}
}
