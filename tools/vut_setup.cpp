#include "vut/assignment_store.hpp"
#include "vut/backend.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <sstream>
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
void help() {
    std::cout << "Setup Altra Trackers / VIVE Ultimate Tracker\n"
        "Commands: refresh, identify, confirm RF_ADDRESS, next, roles,\n"
        "  assign RF_ADDRESS ROLE [cancel|swap|replace],\n"
        "  reassign STABLE_ID ROLE [cancel|swap|replace], unassign STABLE_ID,\n"
        "  swap STABLE_ID STABLE_ID, clear-all confirm, validate, save, quit\n"
        "Pairing: stop this tool, run vut_pair, put the tracker in pairing mode,\n"
        "then return here. Normal scanning never starts physical pairing.\n"
        "Move just ONE tracker after identify; inspect metrics then confirm its RF address.\n"
        "Current HID backend exposes RF addresses only. Stable ID/serial/UUID are unavailable.\n"
        "Persistent assignment to discovered hardware is BLOCKED until a verified identity\n"
        "provider is integrated. Saved roles are local setup data; no upper consumer role API\n"
        "exists here. No Monaka body-role output is changed by this tool.\n";
}
vut::setup::ConflictAction conflict(const std::string& text) {
    if (text.empty() || text == "cancel") return vut::setup::ConflictAction::Cancel;
    if (text == "swap") return vut::setup::ConflictAction::Swap;
    if (text == "replace") return vut::setup::ConflictAction::Replace;
    throw std::invalid_argument("choose swap / replace / cancel");
}
void list(const vut::setup::Devices& devices, const vut::setup::Assignments& assignments) {
    const auto now = nowNs();
    for (const auto& [rf, d] : devices.entries()) {
        const auto role = d.identity ? assignments.role(*d.identity) : std::string{};
        std::cout << "\nname=" << d.displayName << "\nrf_address=" << rf
            << " stable_id=" << (d.identity ? d.identity->key() : "UNAVAILABLE")
            << " serial_uuid=" << (d.identity ? d.identity->identifier : "UNAVAILABLE")
            << "\nconnection=" << (d.connected ? "observed_connected" : "disconnected")
            << " tracking=" << vut::trackingStatusName(d.tracking)
            << " assigned_role=" << (role.empty() ? "unassigned" : role)
            << "\nlast_seen_age_ms=" << (d.lastSeenNs ? (now-d.lastSeenNs)/1000000 : -1)
            << " pose_age_ms=" << (d.lastPoseNs ? (now-d.lastPoseNs)/1000000 : -1)
            << " pose_reception=" << (d.connected && d.lastPoseNs && now-d.lastPoseNs <= 250000000 ? "fresh" : "stale_or_none")
            << "\nmotion_samples=" << d.motion.samples << " movements=" << d.motion.movements
            << " position_delta_m=" << d.motion.positionDeltaMeters
            << " angular_delta_deg=" << d.motion.angularDeltaRadians*180/3.141592653589793
            << " warning=" << (d.warning.empty() ? "none" : d.warning) << '\n';
    }
    if (devices.entries().empty()) std::cout << "No tracker events observed yet. Power on a paired tracker and connect the official dongle.\n";
    if (!devices.warning().empty()) std::cerr << "Device discovery warning: " << devices.warning() << '\n';
    for (const auto& [id, role] : assignments.entries()) {
        bool connected = false;
        for (const auto& [_, d] : devices.entries())
            if (d.connected && d.identity && d.identity->key() == id) connected = true;
        if (!connected) std::cout << "saved_stable_id=" << id << " role=" << role << " connection=unresolved_or_disconnected (retained)\n";
    }
    const auto candidates = devices.motionCandidates(now);
    std::cout << "Motion candidates (human confirmation required):";
    for (const auto& rf : candidates) std::cout << ' ' << rf;
    if (candidates.empty()) std::cout << " none";
    if (candidates.size() > 1) std::cout << " AMBIGUOUS; repeat identify with one tracker";
    std::cout << '\n';
}
class Commands {
public:
    Commands(vut::setup::Devices& d, vut::setup::AssignmentStore& s) : devices(d), store(s) {}
    bool execute(const std::string& line) {
        std::istringstream row(line); std::string command; row >> command;
        std::vector<std::string> args; std::string arg;
        while (row >> std::quoted(arg)) args.push_back(arg);
        if (!row.eof()) throw std::invalid_argument("invalid command quoting");
        const auto arity = [&](std::size_t low, std::size_t high) {
            if (args.size() < low || args.size() > high) throw std::invalid_argument("unexpected command arguments; use help");
        };
        auto& assignments = store.assignments();
        if (command.empty()) return true;
        if (command == "quit") { arity(0,0); return false; }
        if (command == "help") { arity(0,0); help(); }
        else if (command == "refresh") { arity(0,0); list(devices, assignments); }
        else if (command == "roles") { arity(0,0); for (const auto& role : vut::setup::roles()) std::cout << role << '\n'; }
        else if (command == "identify") {
            arity(0,0); confirmed.reset(); devices.beginIdentify(nowNs());
            std::cout << "Move one tracker for up to 5 seconds; refresh to inspect candidates, then confirm RF_ADDRESS.\n";
        } else if (command == "confirm") {
            arity(1,1); const auto candidates = devices.motionCandidates(nowNs());
            if (candidates.size() != 1 || candidates.front() != args[0])
                throw std::invalid_argument("no unique fresh motion candidate; repeat identify");
            confirmed = args[0];
            std::cout << "Motion identity confirmed for " << *confirmed << "; stable hardware identity still required for persistence.\n";
        } else if (command == "next") {
            arity(0,0);
            for (const auto& [rf, d] : devices.entries())
                if (d.connected && (!d.identity || assignments.role(*d.identity).empty())) {
                    std::cout << "Next unassigned: " << rf << "; use identify then confirm.\n"; return true;
                }
            std::cout << "No connected unassigned tracker.\n";
        } else if (command == "assign") {
            arity(2,3); const auto it = devices.entries().find(args[0]);
            if (it == devices.entries().end() || !it->second.connected) throw std::invalid_argument("device is not connected");
            if (!confirmed || *confirmed != args[0] || devices.motionCandidates(nowNs()) != std::vector<std::string>{args[0]})
                throw std::invalid_argument("human-confirmed fresh Identify required");
            if (!it->second.identity) throw std::invalid_argument("stable hardware identity unavailable; RF address/slot will NOT be saved as identity");
            assignments.assign(*it->second.identity, args[1], conflict(args.size() == 3 ? args[2] : ""));
            confirmed.reset(); dirty = true;
        } else if (command == "reassign") {
            arity(2,3); const auto id = vut::setup::parseStableIdentity(args[0]);
            if (assignments.role(id).empty()) throw std::invalid_argument("unknown saved stable identity");
            assignments.assign(id, args[1], conflict(args.size() == 3 ? args[2] : "")); dirty = true;
        } else if (command == "unassign") {
            arity(1,1); const auto id = vut::setup::parseStableIdentity(args[0]);
            if (assignments.role(id).empty()) throw std::invalid_argument("unknown saved stable identity");
            assignments.unassign(id); dirty = true;
        } else if (command == "swap") {
            arity(2,2); const auto a = vut::setup::parseStableIdentity(args[0]), b = vut::setup::parseStableIdentity(args[1]);
            if (assignments.role(a).empty() || assignments.role(b).empty()) throw std::invalid_argument("swap requires two saved identities");
            assignments.swap(a,b); dirty = true;
        } else if (command == "clear-all") {
            arity(1,1); if (args[0] != "confirm") throw std::invalid_argument("use clear-all confirm");
            assignments.clear(); dirty = true;
        } else if (command == "validate") {
            arity(0,0);
            std::cout << "Local assignment schema/roles/uniqueness valid; " << assignments.entries().size()
                << " saved identities. Hardware auto-restore and upper body-role application: BLOCKED.\n";
        } else if (command == "save") {
            arity(0,0); store.save(); dirty = false;
            std::cout << "Saved local assignment sidecar atomically. Upper consumer role application is unavailable.\n";
        } else throw std::invalid_argument("unknown command; use help");
        return true;
    }
    bool dirty = false;
private:
    vut::setup::Devices& devices;
    vut::setup::AssignmentStore& store;
    std::optional<std::string> confirmed;
};
}
int main(int argc, char** argv) try {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "vut_setup ASSIGNMENTS.json [--offline|--validate]\n"; help(); return 0;
    }
    if (argc < 2 || argc > 3) throw std::invalid_argument("use vut_setup ASSIGNMENTS.json [--offline|--validate]");
    const std::string mode = argc == 3 ? argv[2] : "";
    if (!mode.empty() && mode != "--offline" && mode != "--validate") throw std::invalid_argument("unknown setup option");
    vut::setup::AssignmentStore store(argv[1]);
    vut::setup::Devices devices;
    Commands commands(devices, store);
    if (mode == "--validate") { commands.execute("validate"); return 0; }
    help();
    if (mode == "--offline") {
        std::string line;
        while (std::getline(std::cin, line)) {
            try { if (!commands.execute(line)) break; }
            catch (const std::exception& e) { std::cerr << "ERROR: " << e.what() << '\n'; }
        }
    } else {
#ifdef _WIN32
        DWORD consoleMode = 0;
        if (!GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &consoleMode))
            throw std::runtime_error("live setup requires an interactive console; use --offline for scripted edits");
        std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
        vut::Backend backend;
        vut::BackendConfig config; config.exclusiveHidAccess = true;
        backend.setEventCallback([&](const auto& event) { devices.observe(event); });
        std::string input;
        bool started = false;
        auto retryAt = std::chrono::steady_clock::now();
        std::cout << "> " << std::flush;
        while (running) {
            const auto now = std::chrono::steady_clock::now();
            if (!backend.running() && now >= retryAt) {
                if (backend.start(config)) {
                    devices.beginSession(backend.transportGeneration()); started = true;
                    std::cout << "\nDongle connected; passive discovery started.\n> " << input << std::flush;
                } else {
                    std::cerr << "\nConnection warning: " << backend.lastError()
                        << ". Stop vut_service / VIVE Hub before exclusive setup.\n";
                    retryAt = now + std::chrono::seconds(5);
                }
            }
            if (backend.running() && !backend.pollOnce()) {
                std::cerr << "\nTransport error: " << backend.lastError() << '\n';
                devices.endSession(); started = false; retryAt = now + std::chrono::seconds(1);
            }
            if (!started) Sleep(50);
            while (_kbhit()) {
                const auto ch = _getch();
                if (ch == 0 || ch == 224) { (void)_getch(); continue; }
                if (ch == 3) { running = false; break; }
                if (ch == 13) {
                    std::cout << '\n';
                    try { if (!commands.execute(input)) running = false; }
                    catch (const std::exception& e) { std::cerr << "ERROR: " << e.what() << '\n'; }
                    input.clear(); if (running) std::cout << "> " << std::flush;
                } else if (ch == 8 && !input.empty()) { input.pop_back(); std::cout << "\b \b" << std::flush; }
                else if (ch >= 32 && ch < 127 && input.size() < 2048) { input += static_cast<char>(ch); std::cout << static_cast<char>(ch) << std::flush; }
            }
        }
#else
        throw std::runtime_error("live dongle setup is supported on Windows only; use --offline");
#endif
    }
    if (commands.dirty) std::cerr << "Unsaved assignment changes were discarded. Use save before quit.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "ERROR: " << error.what() << '\n'; return 1;
}
