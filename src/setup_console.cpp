#include "vut/setup_console.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace vut::setup {
Command parseCommand(std::string_view line) {
    if (line.size() > 4096 || std::any_of(line.begin(), line.end(), [](unsigned char c) {
        return (c < 0x20 && c != '\t') || c == 0x7f;
    })) throw std::invalid_argument("malformed/oversized command");
    std::istringstream row{std::string(line)};
    Command command;
    row >> command.name;
    for (;;) {
        row >> std::ws;
        if (row.eof()) break;
        std::string arg;
        if (!(row >> std::quoted(arg))) throw std::invalid_argument("invalid command quoting");
        command.args.push_back(std::move(arg));
    }
    return command;
}
void printSetupHelp(std::ostream& out) {
    out << "Setup Altra Trackers / VIVE Ultimate Tracker\n"
        "Commands: refresh, identify, confirm RF_ADDRESS, next, quit, help\n"
        "  pair [all|0|1|2|3|4], pair-cancel, pair-status\n"
        "Pair timeout: 60 seconds. Put the tracker in physical pairing mode after pair.\n"
        "Pair success keeps this session open for listing and Identify.\n"
        "Identify policy: provisional / HIL_REQUIRED. Move ONE tracker, refresh, then confirm.\n"
        "Stable per-tracker identity is unavailable; persistent assignment is BLOCKED.\n"
        "Body targets are owned by MonakaVR; no body-role store or apply API is exposed here.\n"
        "Direct exclusive HID is a bring-up path: stop service/Hub first; final live UX requires a service control API.\n";
}
void printPairingResult(std::ostream& out, const PairingResult& result) {
    out << "pair_state=" << pairingStateName(result.state)
        << " disable=" << (result.cleanupConfirmed ? "confirmed_or_not_enabled" : "UNCONFIRMED")
        << " reason=" << result.reason << '\n';
    if (result.paired) out << "Paired: rf_address=" << result.paired->toString()
        << " slot=" << result.paired->slot << " stable_id=UNAVAILABLE assignment=BLOCKED\n";
    if (!result.cleanupError.empty()) out << "WARNING: PairDevice disable unconfirmed: " << result.cleanupError << '\n';
}
namespace {
void list(const Devices& devices, std::int64_t now, std::ostream& out) {
    for (const auto& [rf, d] : devices.entries()) {
        const bool fresh = d.connected && d.lastPoseNs && now >= d.lastPoseNs &&
            now - d.lastPoseNs <= devices.identifyPolicy().freshnessNs;
        out << "\nname=" << d.displayName << "\nrf_address=" << rf
            << " stable_id=" << (d.identity ? d.identity->key() : "UNAVAILABLE")
            << "\nconnection=" << (d.connected ? "observed_connected" : "disconnected")
            << " tracking=" << trackingStatusName(d.tracking) << " body_assignment=UNAVAILABLE\n"
            << "last_seen_age_ms=" << (d.lastSeenNs && now >= d.lastSeenNs ? (now-d.lastSeenNs)/1000000 : -1)
            << " pose_age_ms=" << (d.lastPoseNs && now >= d.lastPoseNs ? (now-d.lastPoseNs)/1000000 : -1)
            << " pose_reception=" << (fresh ? "fresh" : "stale_or_none")
            << "\nmotion_samples=" << d.motion.samples << " movements=" << d.motion.movements
            << " position_delta_m=" << d.motion.positionDeltaMeters
            << " angular_delta_rad=" << d.motion.angularDeltaRadians
            << " max_step_m=" << d.motion.maxPositionStepMeters
            << " max_step_rad=" << d.motion.maxAngularStepRadians
            << " interval_ms=" << d.motion.lastIntervalNs/1000000.0
            << " max_gap_ms=" << d.motion.maxGapNs/1000000.0
            << " warning=" << (d.warning.empty() ? "none" : d.warning) << '\n';
    }
    if (devices.entries().empty()) out << "No tracker events observed yet.\n";
    if (!devices.warning().empty()) out << "Device discovery warning: " << devices.warning() << '\n';
    const auto candidates = devices.motionCandidates(now);
    out << "Motion candidates (human confirmation required):";
    for (const auto& rf : candidates) out << ' ' << rf;
    if (candidates.empty()) out << " none";
    if (candidates.size() > 1) out << " AMBIGUOUS; repeat identify with one tracker";
    out << '\n';
}
}
bool SetupConsole::execute(const Command& command, std::int64_t now, std::ostream& out) {
    const auto& name = command.name;
    const auto& args = command.args;
    const auto arity = [&](std::size_t low, std::size_t high) {
        if (args.size() < low || args.size() > high) throw std::invalid_argument("unexpected command arguments; use help");
    };
    auto& pairing = session_.pairing();
    auto& devices = session_.devices();
    if (name.empty()) return true;
    if (name == "help") { arity(0,0); printSetupHelp(out); }
    else if (name == "refresh") { arity(0,0); list(devices, now, out); }
    else if (name == "pair-status") { arity(0,0); printPairingResult(out, pairing.result()); }
    else if (name == "pair") {
        arity(0,1);
        int slot = -1;
        if (!args.empty() && args[0] != "all") {
            if (args[0].size() != 1 || args[0][0] < '0' || args[0][0] > '4')
                throw std::invalid_argument("pair selector must be all or 0..4");
            slot = args[0][0] - '0';
        }
        if (!pairing.begin(now, slot)) {
            printPairingResult(out, pairing.result());
            throw std::runtime_error("pair enable failed: " + pairing.result().reason);
        }
        out << "Put VIVE Ultimate Tracker into pairing mode.\n";
        printPairingResult(out, pairing.result());
    } else if (name == "pair-cancel") {
        arity(0,0); const bool cleaned = pairing.cancel(); printPairingResult(out, pairing.result());
        if (!cleaned) throw std::runtime_error("pair cleanup failed");
    } else if (name == "identify") {
        arity(0,0);
        if (pairing.active()) throw std::invalid_argument("finish/cancel pairing before Identify");
        devices.beginIdentify(now);
        out << "Move one tracker for up to " << devices.identifyPolicy().windowNs/1000000000.0
            << " seconds; refresh then confirm RF_ADDRESS (provisional / HIL_REQUIRED).\n";
    } else if (name == "confirm") {
        arity(1,1); const auto candidates = devices.motionCandidates(now);
        if (candidates.size() != 1 || candidates.front() != args[0])
            throw std::invalid_argument("no unique fresh motion candidate; repeat identify");
        out << "Motion confirmed for " << args[0] << "; stable identity and body assignment remain unavailable.\n";
    } else if (name == "next") {
        arity(0,0);
        for (const auto& [rf, d] : devices.entries()) if (d.connected) {
            out << "Connected tracker: " << rf << "; use identify then confirm. Body assignment is unavailable.\n";
            return true;
        }
        out << "No connected tracker.\n";
    } else if (name == "quit") {
        arity(0,0); const bool cleaned = session_.shutdown(); printPairingResult(out, pairing.result());
        if (!cleaned) out << "WARNING: leaving with unconfirmed PairDevice disable.\n";
        return false;
    } else throw std::invalid_argument("unknown command; use help");
    return true;
}
CommandResult SetupConsole::process(std::string_view line, std::int64_t now, std::ostream& out, std::ostream& errors) {
    try {
        const bool keepRunning = execute(parseCommand(line), now, out);
        return {keepRunning, keepRunning || session_.pairing().result().cleanupConfirmed};
    }
    catch (const std::exception& e) { errors << "ERROR: " << e.what() << '\n'; return {true, false}; }
}
}
