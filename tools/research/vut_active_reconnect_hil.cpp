#include "active_reconnect_session.hpp"
#include "active_reconnect_windows.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <thread>

namespace {
using namespace vut::active_hil;
bool end(std::istream& input) { std::string extra; return !(input >> extra); }
bool binding(std::istream& input, Binding& value) {
    std::string address;
    if (!(input >> value.device.slot >> address >> value.generation >> value.observationEpoch >> value.connectionEpoch) || address.size() != 12)
        return false;
    for (std::size_t i = 0; i < 6; ++i) {
        auto digit = [](char c) -> int { return c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : -1; };
        const int a = digit(address[i*2]), b = digit(address[i*2+1]); if (a < 0 || b < 0) return false;
        value.device.mac[i] = static_cast<std::uint8_t>((a << 4) | b);
    }
    return value.device.slot >= 0 && value.device.slot < 5 && value.device.slot == vut::protocol::slotFromMac(value.device.mac);
}
bool gate(std::string_view text, Gate& g) {
    if (text == "B2-1") g = Gate::B21; else if (text == "B2-2") g = Gate::B22;
    else if (text == "Q1E") g = Gate::Q1E; else return false; return true;
}
void control(std::string_view text, Session& session, std::int64_t now) {
    std::istringstream input{std::string(text)}; std::string command; input >> command;
    if (command == "b_on_requested" && end(input)) session.requestBOn(now);
    else if (command == "b_on_confirmed" && end(input)) session.confirmBOn(now);
    else if (command == "capture-proof") {
        std::string run; std::int64_t stamp = -1;
        if (input >> run >> stamp && end(input)) session.captureProof(run, stamp, now); else session.stop("InvalidControl");
    } else if (command == "bind-B") {
        Binding b; std::string confirmation;
        if (binding(input, b) && input >> confirmation && end(input) && confirmation == "B_ONLY_REGISTERED_A_OFF")
            session.bindB(b, true, now);
        else session.stop("MissingHumanBConfirmation");
    } else if (command == "--send-behavior2-once" || command == "--send-query1e-once") {
        Approval a; a.flag = command; std::string name;
        if (input >> name && gate(name, a.gate) && binding(input, a.binding) &&
            input >> a.run >> a.token >> a.alias && end(input)) session.send(a, now);
        else session.stop("MissingOrWrongApproval");
    } else if (command == "finish" && end(input))
        session.stop(session.attempts()[0] ? "HIL-D" : "HIL-F-NoActiveTrial");
    else session.stop("InvalidControl");
}
}

int main(int argc, char** argv) {
    using namespace vut::active_hil;
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "vut_active_reconnect_hil [--observe] --run-dir PRIVATE_EXISTING_DIRECTORY\n"
            "Default: observe only. Research target requires explicit CMake opt-in.\n"
            "No startup/background send, retry, Pair, Unpair or recovery commands.\n"
            "Private request files carry individually approved --send-behavior2-once / --send-query1e-once,\n"
            "exact current B binding and gate-specific nonce. Tokens alone are not human approval.\n"
            "B OFF baseline >=5s; B ON baseline >=10s; each post-send window >=10s.\n"
            "Stop capture and audit before creating shutdown.txt; sole HID ownership is held at terminal result.\n";
        return 0;
    }
    std::filesystem::path directory;
    bool observeFlag = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--observe" && !observeFlag) observeFlag = true;
        else if (arg == "--run-dir" && directory.empty() && i+1 < argc) directory = argv[++i];
        else { std::cerr << "InvalidArguments\n"; return 3; }
    }
    if (directory.empty() || !std::filesystem::is_directory(directory)) { std::cerr << "PrivateRunDirectoryRequired\n"; return 3; }
    try {
        // One process per directory; persistent lock is never removed or reused.
        DurableJournal lock(directory / "owner.lock");
        const auto run = nonce(); std::array<std::string, 3> tokens{nonce(),nonce(),nonce()};
        lock.write(run + "\n");
        DurableJournal log(directory / "runner-private.jsonl");
        const auto started = std::chrono::steady_clock::now();
        auto elapsed = [&] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count(); };
        ResearchHid hid;
        auto audit = [&](const SendReceipt& receipt) {
            std::ostringstream out;
            out << "{\"kind\":\"send\",\"run\":\"" << run << "\",\"gate\":\"" << gateName(receipt.gate)
                << "\",\"elapsed_ms\":" << receipt.elapsedMs << ",\"generation\":" << receipt.binding.generation
                << ",\"observation_epoch\":" << receipt.binding.observationEpoch << ",\"connection_epoch\":" << receipt.binding.connectionEpoch
                << ",\"slot\":" << receipt.binding.device.slot << ",\"target_alias\":\"B\",\"rf\":\"" << hex(receipt.binding.device.mac)
                << "\",\"request_category\":\"" << flagName(receipt.gate) << "\",\"request_sha256\":\"" << sha256(receipt.request)
                << "\",\"request_hex\":\"" << hex(receipt.request) << "\",\"send_attempted\":" << receipt.attempted
                << ",\"transport_returned\":" << receipt.returned << ",\"transport_accepted\":" << receipt.accepted
                << ",\"response_category\":\"" << receipt.response << "\",\"response_hex\":\"" << hex(receipt.responseBytes) << "\"}\n";
            if (!receipt.attempted) {
                // Persistent one-trial + per-gate intent consumes permission BEFORE API.
                if (receipt.gate == Gate::B21) { DurableJournal trial(directory / "trial.lock"); trial.write(run + "\n"); }
                DurableJournal once(directory / (std::string(gateName(receipt.gate)) + ".attempt")); once.write(out.str());
            }
            log.write(out.str());
        };
        Session session(run, tokens, hid, audit);
        std::string error;
        if (!hid.open(vut::protocol::kVendorIdVive, vut::protocol::kProductIdUltimateTrackerDongle, true, error)) {
            log.write("{\"kind\":\"runtime\",\"reason\":\"ExclusiveOpenFailed\",\"detail\":\"" + error + "\"}\n");
            std::cout << "ExclusiveOpenFailed " << error << "\n"; return 2;
        }
        session.opened(1);
        log.write("{\"kind\":\"runtime\",\"reason\":\"ExclusiveOpenSucceeded\",\"generation\":1}\n");
        std::cout << "ExclusiveOpenSucceeded ObserveOnly\n" << std::flush;
        std::set<std::filesystem::path> processed;
        std::int64_t confirmedAt = -1, terminalAt = -1, statusAt = -1;
        auto status = [&](std::int64_t now) {
            const auto b = session.currentBinding(); const auto counts = session.attempts();
            std::ostringstream out;
            out << "{\"run\":\"" << run << "\",\"elapsed_ms\":" << now << ",\"generation\":" << b.generation
                << ",\"observation_epoch\":" << b.observationEpoch << ",\"connection_epoch\":" << b.connectionEpoch
                << ",\"slot\":" << b.device.slot << ",\"rf\":\"" << hex(b.device.mac) << "\",\"candidate\":" << bool(session.candidate())
                << ",\"b_confirmed\":" << session.confirmed() << ",\"bound\":" << session.bound()
                << ",\"fresh_identity\":" << session.freshIdentity() << ",\"pose\":" << session.pose()
                << ",\"recovered\":" << session.recovered() << ",\"pairs\":" << session.pairs() << ",\"unpairs\":" << session.unpairs()
                << ",\"rf_reports\":" << session.rfReports() << ",\"next_gate_at_ms\":" << session.nextGateAt()
                << ",\"rf_transitions\":" << session.rfTransitions() << ",\"opaque_rf_record\":\""
                << (session.rfRecord() ? hex(*session.rfRecord()) : "") << "\""
                << ",\"attempts\":[" << counts[0] << ',' << counts[1] << ',' << counts[2] << "],\"reason\":\"" << session.reason()
                << "\",\"gate_tokens\":[\"" << tokens[0] << "\",\"" << tokens[1] << "\",\"" << tokens[2] << "\"],\"epochs\":[";
            bool first = true;
            for (const auto& e : session.epochs()) {
                if (!first) out << ','; first = false;
                out << "{\"id\":" << e.connectionEpochId << ",\"generation\":" << e.transportGeneration
                    << ",\"active\":" << e.active << ",\"progress\":" << unsigned(e.expectedIndex)
                    << ",\"info_complete\":" << e.infoComplete << ",\"initialization_eligible\":" << e.initializationEligible
                    << ",\"pose_observed\":" << e.poseObserved << ",\"reason\":" << static_cast<unsigned>(e.reason) << '}';
            }
            out << "]}\n";
            const auto temporary = directory / "status-private.tmp", target = directory / "status-private.json";
            { std::ofstream file(temporary, std::ios::binary | std::ios::trunc); file << out.str(); file.flush();
              if (!file) throw std::runtime_error("StatusUnavailable"); }
            if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("StatusUnavailable");
        };
        try {
            for (;;) {
                const auto now = elapsed();
                if (std::filesystem::exists(directory / "shutdown.txt")) break;
                if (now >= 900000) session.stop("InfrastructureDeadline");
                if (!session.stopped()) {
                    const auto report = hid.read(50);
                    if (hid.readFailed()) session.transportLost();
                    else if (!report.empty()) {
                        log.write("{\"kind\":\"raw\",\"elapsed_ms\":" + std::to_string(elapsed()) + ",\"report_hex\":\"" + hex(report) + "\"}\n");
                        session.observe(report, elapsed());
                    }
                    if (!session.stopped()) for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                        if (entry.path().extension() != ".request" || processed.contains(entry.path())) continue;
                        processed.insert(entry.path());
                        if (processed.size() > 32 || std::filesystem::file_size(entry.path()) > 2048) { session.stop("ControlLimit"); break; }
                        std::ifstream file(entry.path(), std::ios::binary); std::string text((std::istreambuf_iterator<char>(file)), {});
                        if (text.empty() || file.bad()) { session.stop("ControlUnavailable"); break; }
                        log.write("{\"kind\":\"operator_control\",\"elapsed_ms\":" + std::to_string(elapsed()) + ",\"command_sha256\":\"" +
                                  sha256(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size())) + "\"}\n");
                        const auto before = session.attempts();
                        control(text, session, elapsed());
                        if (session.attempts() != before) session.sendReturnedAt(elapsed());
                        if (session.stopped()) break;
                    }
                    if (session.confirmed() && confirmedAt < 0) confirmedAt = elapsed();
                    if (confirmedAt >= 0 && elapsed() - confirmedAt >= 30000 && !session.bound()) session.stop("CurrentBMappingUnavailable");
                }
                if (session.stopped()) {
                    if (terminalAt < 0) {
                        terminalAt = elapsed();
                        log.write("{\"kind\":\"terminal\",\"elapsed_ms\":" + std::to_string(terminalAt) + ",\"reason\":\"" + session.reason() + "\"}\n");
                        std::cout << "ObservationFinished " << session.reason() << " OwnershipHeld\n" << std::flush;
                    }
                    if (elapsed() - terminalAt >= 180000) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                if (elapsed() - statusAt >= 250) { status(elapsed()); statusAt = elapsed(); }
            }
        } catch (...) {
            session.stop("ProcessException");
            try { status(elapsed()); log.write("{\"kind\":\"terminal\",\"reason\":\"ProcessException\"}\n"); } catch (...) {}
            // No TX on exception. Keep ownership briefly for capture finalization.
            const auto until = elapsed() + 180000;
            while (elapsed() < until && !std::filesystem::exists(directory / "shutdown.txt")) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        hid.close(); log.write("{\"kind\":\"runtime\",\"reason\":\"NormalShutdown\"}\n");
        std::cout << "NormalShutdown\n"; return session.recovered() ? 0 : 2;
    } catch (...) { std::cerr << "InfrastructureFailureNoRecovery\n"; return 3; }
}
