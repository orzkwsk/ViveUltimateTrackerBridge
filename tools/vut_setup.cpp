#include "vut/setup_console.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#endif

namespace {
std::atomic_bool running{true};
void stop(int) { running = false; }
std::int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
int main(int argc, char** argv) try {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "vut_setup [--offline]\n";
        vut::setup::printSetupHelp(std::cout); return 0;
    }
    if (argc > 2 || (argc == 2 && std::string(argv[1]) != "--offline"))
        throw std::invalid_argument("use vut_setup [--offline]; assignment sidecars are internal experiments only");
    const bool offline = argc == 2;
    vut::Backend backend;
    vut::setup::SetupSession session(backend);
    vut::setup::SetupConsole console(session);
    vut::setup::printSetupHelp(std::cout);
    bool commandFailed = false;
    if (offline) {
        std::string line;
        while (std::getline(std::cin, line)) {
            const auto result = console.process(line, nowNs(), std::cout, std::cerr);
            commandFailed |= !result.succeeded;
            if (!result.keepRunning) break;
        }
        if (std::cin.bad()) throw std::runtime_error("script input read failed");
    } else {
#ifdef _WIN32
        DWORD consoleMode = 0;
        if (!GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &consoleMode))
            throw std::runtime_error("live setup requires an interactive console; use --offline for scripts");
        std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
        vut::BackendConfig config; config.exclusiveHidAccess = true;
        std::string input;
        auto retryAt = std::chrono::steady_clock::now();
        auto lastPairState = session.pairing().result().state;
        std::cout << "> " << std::flush;
        while (running) {
            const auto now = std::chrono::steady_clock::now();
            if (!backend.running() && now >= retryAt) {
                if (backend.start(config)) {
                    session.connected();
                    std::cout << "\nDongle connected; passive discovery started.\n> " << input << std::flush;
                } else {
                    std::cerr << "\nConnection warning: " << backend.lastError()
                        << ". Stop vut_service / VIVE Hub before exclusive setup.\n";
                    retryAt = now + std::chrono::seconds(5);
                }
            }
            if (backend.running() && !session.pollOnce(nowNs())) {
                std::cerr << "\nTransport error: " << backend.lastError() << '\n';
                retryAt = now + std::chrono::seconds(1);
            }
            const auto state = session.pairing().result().state;
            if (state != lastPairState) {
                std::cout << '\n';
                vut::setup::printPairingResult(std::cout, session.pairing().result());
                std::cout << "> " << input << std::flush;
                lastPairState = state;
            }
            if (!backend.running()) Sleep(50);
            while (_kbhit()) {
                const auto ch = _getch();
                if (ch == 0 || ch == 224) { (void)_getch(); continue; }
                if (ch == 3) { running = false; break; }
                if (ch == 13) {
                    std::cout << '\n';
                    const auto result = console.process(input, nowNs(), std::cout, std::cerr);
                    if (!result.keepRunning) running = false;
                    input.clear(); if (running) std::cout << "> " << std::flush;
                } else if (ch == 8 && !input.empty()) { input.pop_back(); std::cout << "\b \b" << std::flush; }
                else if (ch >= 32 && ch < 127 && input.size() < 4096) { input += static_cast<char>(ch); std::cout << static_cast<char>(ch) << std::flush; }
            }
        }
#else
        throw std::runtime_error("live dongle setup is supported on Windows only; use --offline");
#endif
    }
    const bool cleaned = session.shutdown();
    backend.stop();
    if (!cleaned) vut::setup::printPairingResult(std::cerr, session.pairing().result());
    return commandFailed || !cleaned ? 1 : 0;
} catch (const std::exception& error) {
    std::cerr << "ERROR: " << error.what() << '\n'; return 1;
}
