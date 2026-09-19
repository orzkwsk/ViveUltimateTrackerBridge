#include "vut/backend.hpp"

#include <atomic>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic_bool gRun{true};
std::mutex gPrintMutex;

void onSignal(int) { gRun = false; }

void printLine(const std::string& text) {
    std::scoped_lock lock(gPrintMutex);
    std::cout << text << std::endl;
}

std::string toHex(std::span<const std::uint8_t> bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i != 0) out << ' ';
        out << std::setw(2) << static_cast<unsigned>(bytes[i]);
    }
    return out.str();
}

std::string lower(std::string value) {
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::optional<int> parseSlot(const std::string& token) {
    const auto normalized = lower(token);
    if (normalized == "all") return -1;
    try {
        const int value = std::stoi(token);
        if (value >= 0 && value < 5) return value;
    } catch (...) {
    }
    return std::nullopt;
}

std::optional<std::uint8_t> parseHexByte(const std::string& token) {
    try {
        std::size_t consumed = 0;
        const auto value = std::stoul(token, &consumed, 16);
        if (consumed == token.size() && value <= 0xff) return static_cast<std::uint8_t>(value);
    } catch (...) {
    }
    return std::nullopt;
}

const char* modeName(vut::UnpairRecoveryMode mode) {
    using M = vut::UnpairRecoveryMode;
    switch (mode) {
    case M::SlotSpecificRestartRf: return "slot-restart";
    case M::None: return "none";
    case M::ColdStartRestartRf: return "cold-restart";
    case M::DisablePws: return "pws-off";
    case M::DisablePwsThenRestartRf: return "pws-restart";
    case M::DisablePairThenRestartRf: return "pairoff-restart";
    case M::DisablePairAndPwsThenRestartRf: return "pairoff-pws-restart";
    case M::RfRebootApp: return "rfreboot";
    case M::RfRebootAppThenRestartRf: return "rfreboot-restart";
    }
    return "unknown";
}

std::optional<vut::UnpairRecoveryMode> parseMode(const std::string& token) {
    using M = vut::UnpairRecoveryMode;
    const auto value = lower(token);
    if (value == "none") return M::None;
    if (value == "slot-restart") return M::SlotSpecificRestartRf;
    if (value == "cold-restart") return M::ColdStartRestartRf;
    if (value == "pws-off") return M::DisablePws;
    if (value == "pws-restart") return M::DisablePwsThenRestartRf;
    if (value == "pairoff-restart") return M::DisablePairThenRestartRf;
    if (value == "pairoff-pws-restart") return M::DisablePairAndPwsThenRestartRf;
    if (value == "rfreboot") return M::RfRebootApp;
    if (value == "rfreboot-restart") return M::RfRebootAppThenRestartRf;
    return std::nullopt;
}

void printResult(const vut::DebugCommandResult& result) {
    std::ostringstream out;
    out << "[result] cmd=0x" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(result.command)
        << " payload=[" << toHex(result.payload) << ']'
        << std::dec
        << " sent=" << result.sent
        << " responseMatched=" << result.responseMatched;
    if (!result.response.empty()) out << " response=[" << toHex(result.response) << ']';
    if (!result.error.empty()) out << " note=" << result.error;
    printLine(out.str());
}

void printHelp() {
    printLine(
        "Commands:\n"
        "  help\n"
        "  query                         Send direct RF-status query (0x1E)\n"
        "  restart <0-4|all>             RestartRf, enable=1\n"
        "  pair-on <0-4|all>             PairDevice, enable=1\n"
        "  pair-off <0-4|all>            PairDevice, enable=0\n"
        "  pws-on <0-4|all>              RxPowerSaving, enable=1\n"
        "  pws-off <0-4|all>             RxPowerSaving, enable=0\n"
        "  rf-reboot-app                 Candidate 0x1C {00 03}; experiment only\n"
        "  raw <cmdHex> [byteHex ...]    Send arbitrary feature command\n"
        "  policy <name>                 Set automatic action on DeviceUnpaired\n"
        "  delay <ms>                    Set inter-command delay for policy sequences\n"
        "  policy-show                   Show current automatic action/delay\n"
        "  snapshot                      Print current tracker-state snapshot\n"
        "  pose <on|off>                 Toggle pose event logging\n"
        "  quit\n"
        "\n"
        "Policy names:\n"
        "  none | pws-off | slot-restart | cold-restart | pws-restart |\n"
        "  pairoff-restart | pairoff-pws-restart | rfreboot | rfreboot-restart\n"
        "\n"
        "Notes:\n"
        "  - DebugConsole starts with policy=none so a stuck state is not changed automatically.\n"
        "  - slot0/slot2 RestartRf selectors are captured; slot1/3/4 one-hot selectors remain inferred.\n"
        "  - rf-reboot-app is public-RE/USBTool-informed, not vendor-confirmed.\n"
        "  - destructive FactoryReset / clear-pairing candidates are intentionally not exposed as named commands."
    );
}

struct InputState {
    std::mutex mutex;
    std::queue<std::string> lines;
};

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, onSignal);

    bool waitForDongle = false;
    bool noStdin = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--wait-for-dongle") {
            waitForDongle = true;
        } else if (arg == "--no-stdin") {
            noStdin = true;
        } else if (arg == "--help" || arg == "-h") {
            printLine("Options: --wait-for-dongle --no-stdin");
            return 0;
        } else {
            printLine("unknown option: " + arg);
            return 2;
        }
    }

    constexpr const char* kBuildMarker = "VUT_DEBUG_CONSOLE_PWS_OFF_V2";
    printLine(std::string("[build] marker=") + kBuildMarker);

    auto input = std::make_shared<InputState>();
    if (!noStdin) {
        std::thread([input] {
            std::string line;
            while (std::getline(std::cin, line)) {
                std::scoped_lock lock(input->mutex);
                input->lines.push(line);
            }
            std::scoped_lock lock(input->mutex);
            input->lines.push("quit");
        }).detach();
    } else {
        printLine("[config] stdin command reader=disabled");
    }

    std::atomic_bool showPose{false};
    const auto started = std::chrono::steady_clock::now();

    vut::Backend backend;
    backend.setEventCallback([&](const vut::BackendEvent& event) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();

        std::ostringstream out;
        out << "[+" << elapsed << "ms] ";

        if (event.kind == vut::BackendEvent::Kind::DevicePaired) {
            out << "[pair] slot=" << event.device.slot << " addr=" << event.device.toString();
        } else if (event.kind == vut::BackendEvent::Kind::DeviceUnpaired) {
            out << "[unpair] slot=" << event.device.slot << " addr=" << event.device.toString();
        } else if (event.kind == vut::BackendEvent::Kind::Ack) {
            out << "[ack] slot=" << event.device.slot << ' ' << event.text;
        } else if (event.kind == vut::BackendEvent::Kind::Diagnostic) {
            out << "[diag] " << event.text;
        } else if (event.kind == vut::BackendEvent::Kind::Pose && event.pose) {
            if (!showPose.load()) return;
            out << "[pose] slot=" << event.pose->device.slot
                << " status=" << static_cast<int>(event.pose->trackingStatus)
                << " pvalid=" << event.pose->positionValid
                << " ovalid=" << event.pose->orientationValid;
        } else {
            return;
        }

        printLine(out.str());
    });

    vut::BackendConfig config;
    config.unpairRecoveryMode = vut::UnpairRecoveryMode::None;
    config.debugInterCommandDelayMs = 250;

    bool waitMessagePrinted = false;
    while (gRun && !backend.start(config)) {
        if (!waitForDongle) {
            printLine("start failed: " + backend.lastError());
            return 1;
        }
        if (!waitMessagePrinted) {
            printLine("[wait] dongle unavailable; retrying backend start every 250 ms");
            waitMessagePrinted = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    if (!gRun) return 130;

    printLine(noStdin
        ? "DebugConsole started with policy=none; stdin disabled."
        : "DebugConsole started with policy=none. Type 'help'.");
    printLine("USBPcap remains the primary evidence source; console logs are diagnostic convenience.");

    auto currentMode = vut::UnpairRecoveryMode::None;
    int currentDelayMs = 250;

    while (gRun && backend.running()) {
        if (!backend.pollOnce()) break;

        for (;;) {
            std::string line;
            {
                std::scoped_lock lock(input->mutex);
                if (input->lines.empty()) break;
                line = std::move(input->lines.front());
                input->lines.pop();
            }

            std::istringstream parser(line);
            std::string command;
            parser >> command;
            command = lower(command);
            if (command.empty()) continue;

            if (command == "quit" || command == "exit") {
                gRun = false;
                break;
            }

            if (command == "help" || command == "?") {
                printHelp();
                continue;
            }

            if (command == "query") {
                printResult(backend.debugQueryRfStatus());
                continue;
            }

            if (command == "rf-reboot-app") {
                printLine("[warning] sending experiment-only RF reboot APP candidate 0x1C {00 03}");
                printResult(backend.debugRfRebootApp());
                continue;
            }

            if (command == "snapshot") {
                const auto states = backend.snapshot();
                if (states.empty()) {
                    printLine("[snapshot] no active tracker states");
                }
                for (const auto& state : states) {
                    std::ostringstream out;
                    out << "[snapshot] slot=" << state.device.slot
                        << " addr=" << state.device.toString()
                        << " status=" << static_cast<int>(state.trackingStatus)
                        << " pvalid=" << state.positionValid
                        << " ovalid=" << state.orientationValid
                        << " pos=(" << state.positionMeters.x << ','
                        << state.positionMeters.y << ',' << state.positionMeters.z << ')';
                    printLine(out.str());
                }
                continue;
            }

            if (command == "pose") {
                std::string value;
                parser >> value;
                value = lower(value);
                if (value == "on") {
                    showPose = true;
                    printLine("[config] pose logging=on");
                } else if (value == "off") {
                    showPose = false;
                    printLine("[config] pose logging=off");
                } else {
                    printLine("usage: pose <on|off>");
                }
                continue;
            }

            if (command == "policy-show") {
                std::ostringstream out;
                out << "[config] policy=" << modeName(currentMode)
                    << " interCommandDelayMs=" << currentDelayMs;
                printLine(out.str());
                continue;
            }

            if (command == "delay") {
                int value = -1;
                parser >> value;
                if (value < 0 || value > 60000) {
                    printLine("usage: delay <0..60000 ms>");
                    continue;
                }
                currentDelayMs = value;
                backend.setUnpairRecoveryMode(currentMode, currentDelayMs);
                printLine("[config] interCommandDelayMs=" + std::to_string(currentDelayMs));
                continue;
            }

            if (command == "policy") {
                std::string value;
                parser >> value;
                const auto mode = parseMode(value);
                if (!mode) {
                    printLine("unknown policy; type 'help' for names");
                    continue;
                }
                currentMode = *mode;
                backend.setUnpairRecoveryMode(currentMode, currentDelayMs);
                printLine(std::string("[config] policy=") + modeName(currentMode));
                continue;
            }

            if (command == "raw") {
                std::string commandToken;
                parser >> commandToken;
                const auto rawCommand = parseHexByte(commandToken);
                if (!rawCommand) {
                    printLine("usage: raw <cmdHex> [byteHex ...]");
                    continue;
                }

                std::vector<std::uint8_t> payload;
                std::string byteToken;
                bool valid = true;
                while (parser >> byteToken) {
                    const auto byte = parseHexByte(byteToken);
                    if (!byte) {
                        valid = false;
                        break;
                    }
                    payload.push_back(*byte);
                }
                if (!valid) {
                    printLine("raw payload contains a non-byte hex token");
                    continue;
                }

                printLine("[warning] raw command path bypasses named-command guardrails");
                printResult(backend.debugSendRaw(*rawCommand, payload));
                continue;
            }

            if (command == "restart" || command == "pair-on" || command == "pair-off" ||
                command == "pws-on" || command == "pws-off") {
                std::string slotToken;
                parser >> slotToken;
                const auto slot = parseSlot(slotToken);
                if (!slot) {
                    printLine("usage: " + command + " <0-4|all>");
                    continue;
                }

                vut::DebugRfBehavior behavior = vut::DebugRfBehavior::RestartRf;
                bool enabled = true;
                if (command == "pair-on") {
                    behavior = vut::DebugRfBehavior::PairDevice;
                } else if (command == "pair-off") {
                    behavior = vut::DebugRfBehavior::PairDevice;
                    enabled = false;
                } else if (command == "pws-on") {
                    behavior = vut::DebugRfBehavior::RxPowerSaving;
                } else if (command == "pws-off") {
                    behavior = vut::DebugRfBehavior::RxPowerSaving;
                    enabled = false;
                }

                printResult(backend.debugSendRfBehavior(behavior, enabled, *slot));
                continue;
            }

            printLine("unknown command: " + command + " (type 'help')");
        }
    }

    if (!backend.running() && !backend.lastError().empty()) {
        printLine("backend stopped: " + backend.lastError());
    }
    backend.stop();
    return 0;
}
