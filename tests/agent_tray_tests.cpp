#include "vut/agent_tray.hpp"
#include "vut/service_options.hpp"
#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
namespace {
std::atomic<HWND> fixtureWindow{nullptr};
std::atomic_int adds{0}, deletes{0}, selected{0};
std::atomic_bool rejectAdd{false}, holdAdd{false}, addEntered{false};
BOOL WINAPI fakeNotify(DWORD action, PNOTIFYICONDATAW data) {
    if (action == NIM_ADD) {
        fixtureWindow = data->hWnd; ++adds; addEntered = true;
        while (holdAdd.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return rejectAdd ? FALSE : TRUE;
    }
    if (action == NIM_DELETE) ++deletes;
    return TRUE;
}
BOOL WINAPI fakePopup(HMENU, UINT, int, int, int, HWND, const RECT*) { return selected.load(); }
}
#define Shell_NotifyIconW fakeNotify
#define TrackPopupMenu fakePopup
#include "../src/agent_tray.cpp"
#undef TrackPopupMenu
#undef Shell_NotifyIconW
#define CHECK(x) do { if (!(x)) throw std::runtime_error("CHECK failed: " #x); } while (false)
template<class F> void until(F test) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!test()) {
        if (std::chrono::steady_clock::now() > deadline) { holdAdd = false; throw std::runtime_error("tray fixture timeout"); }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
int main() try {
    using namespace vut;
    {
        holdAdd = true;
        AgentTray tray;
        until([] { return addEntered.load(); });
        const auto state = tray.state(); const auto action = tray.takeAction();
        holdAdd = false;
        CHECK(state == AgentTrayState::Starting && action == AgentTrayAction::None);
        until([&] { return tray.state() == AgentTrayState::Visible; });
        const auto hwnd = fixtureWindow.load(); CHECK(hwnd && IsWindow(hwnd));
        SendMessageW(hwnd, WM_APP + 41, 0, WM_LBUTTONDBLCLK);
        CHECK(tray.takeAction() == AgentTrayAction::Open);
        CHECK(tray.takeAction() == AgentTrayAction::None);
        const auto before = adds.load();
        SendMessageW(hwnd, RegisterWindowMessageW(L"TaskbarCreated"), 0, 0);
        CHECK(adds == before + 1 && tray.state() == AgentTrayState::Visible);
        selected = 2;
        SendMessageW(hwnd, WM_APP + 41, 0, WM_LBUTTONDBLCLK);
        SendMessageW(hwnd, WM_APP + 41, 0, WM_RBUTTONUP);
        CHECK(tray.takeAction() == AgentTrayAction::Exit);
        CHECK(tray.takeAction() == AgentTrayAction::None);
    }
    CHECK(deletes == 1 && !IsWindow(fixtureWindow.load()));
    {
        AgentTray tray;
        until([&] { return tray.state() == AgentTrayState::Visible; });
        rejectAdd = true;
        SendMessageW(fixtureWindow.load(), RegisterWindowMessageW(L"TaskbarCreated"), 0, 0);
        CHECK(tray.state() == AgentTrayState::Failed && !tray.responsive());
    }
    { AgentTray tray; until([&] { return tray.state() == AgentTrayState::Failed; }); }
    const std::vector<std::string_view> values{"--gui=C:/test path/Receiver.exe", "--tray", "--diagnostics", "32001"};
    const auto options = parseServiceOptions(values);
    CHECK(options.tray && options.diagnostics && options.port == 32001 && options.guiExecutable == "C:/test path/Receiver.exe");
    for (const auto& args : std::vector<std::vector<std::string_view>>{{"--gui=app.exe"},{"--tray","--tray"},{"--tray","--gui="},{"--tray","--gui=a","--gui=b"}}) {
        bool rejected = false;
        try { (void)parseServiceOptions(args); } catch (const std::invalid_argument&) { rejected = true; }
        CHECK(rejected);
    }
    std::cout << "PASS tray isolation, Open/Exit, Explorer recreation/failure, explicit options\n";
    return 0;
} catch (const std::exception& error) { holdAdd = false; std::cerr << error.what() << '\n'; return 1; }
