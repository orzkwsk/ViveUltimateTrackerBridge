#include "vut/backend.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

namespace {

std::atomic_bool gRun{true};

void onSignal(int) { gRun = false; }

struct Options {
    int slot = -1;
    int timeoutSeconds = 60;
};

void printUsage() {
    std::cout
        << "vut_pair [--slot all|0|1|2|3|4] [--timeout-seconds N]\n\n"
        << "Explicit one-shot first-pairing setup. The default is all existing RF\n"
        << "selectors; vendor assignment of a newly paired tracker to a slot is\n"
        << "UNKNOWN/HIL_REQUIRED. This tool never edits vut-service.conf.\n";
}

bool parsePositive(const char* text, int& value) {
    if (!text || !*text) return false;
    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || parsed <= 0 || parsed > 3600) return false;
    value = static_cast<int>(parsed);
    return true;
}

bool parseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            std::exit(0);
        }
        if (arg == "--slot" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (value == "all") options.slot = -1;
            else if (value.size() == 1 && value[0] >= '0' && value[0] <= '4')
                options.slot = value[0] - '0';
            else return false;
            continue;
        }
        if (arg == "--timeout-seconds" && i + 1 < argc) {
            if (!parsePositive(argv[++i], options.timeoutSeconds)) return false;
            continue;
        }
        return false;
    }
    return true;
}

bool disablePairing(vut::Backend& backend) {
    if (!backend.pairingActive()) return true;
    // One bounded retry covers an uncertain feature response. Backend::stop()
    // makes one final best-effort attempt before closing the HID handle.
    for (int attempt = 0; attempt < 2; ++attempt)
        if (backend.endPairing()) return true;
    return false;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) {
        printUsage();
        return 2;
    }

    std::signal(SIGINT, onSignal);
    vut::Backend backend;
    vut::BackendConfig config;
    config.exclusiveHidAccess = true;

    std::optional<vut::DeviceId> paired;
    backend.setEventCallback([&](const vut::BackendEvent& event) {
        if (event.kind == vut::BackendEvent::Kind::DevicePaired && !paired)
            paired = event.device;
    });

    if (!backend.start(config)) {
        std::cerr << "Cannot open the dongle exclusively: " << backend.lastError() << '\n'
                  << "Stop vut_service and VIVE Hub / VIVE Streaming Hub; no process was stopped by vut_pair.\n";
        return 3;
    }

    std::cout << "Start pairing...\n"
              << "Put VIVE Ultimate Tracker into pairing mode.\n";
    if (!backend.beginPairing(options.slot)) {
        std::cerr << "Failed to enable pairing: " << backend.lastError() << '\n';
        const bool disabled = disablePairing(backend);
        backend.stop();
        if (disabled) std::cout << "Pairing disabled.\n";
        else std::cerr << "WARNING: PairDevice disable could not be confirmed before HID close.\n";
        return 4;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(options.timeoutSeconds);
    bool transportError = false;
    while (gRun && !paired && std::chrono::steady_clock::now() < deadline) {
        if (!backend.pollOnce()) {
            transportError = true;
            break;
        }
    }

    if (paired) {
        std::cout << "\nPaired:\n"
                  << "slot: " << paired->slot << '\n'
                  << "device: " << paired->toString() << '\n';
    }

    const bool disabled = disablePairing(backend);
    const std::string cleanupError = backend.lastError();
    backend.stop();
    if (disabled) std::cout << "\nPairing disabled.\n";
    else std::cerr << "WARNING: PairDevice disable could not be confirmed before HID close: "
                   << cleanupError << '\n';

    if (transportError) {
        std::cerr << "Pairing aborted after HID error: " << cleanupError << '\n';
        return 5;
    }
    if (!gRun) {
        std::cerr << "Pairing cancelled.\n";
        return 6;
    }
    if (!paired) {
        std::cerr << "Pairing timed out after " << options.timeoutSeconds << " seconds.\n";
        return 7;
    }
    if (paired->slot < 0 || paired->slot >= 5) {
        std::cerr << "Pair event had an unsupported slot; configuration suggestion was withheld.\n";
        return 8;
    }
    if (!disabled) return 9;

    std::cout << "Suggested config:\n"
              << "map " << paired->toString() << " tracker-" << paired->slot << "-map\n"
              << "The six-byte RF address is an observed admission key, not a persistent hardware ID.\n";
    return 0;
}
