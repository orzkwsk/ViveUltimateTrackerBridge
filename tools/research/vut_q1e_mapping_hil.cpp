#include "q1e_mapping_session.hpp"
#include "active_reconnect_windows.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <thread>

namespace {
bool end(std::istream& input) { std::string extra; return !(input >> extra); }
void control(std::string_view text, vut::q1e_hil::Session& session, std::int64_t now) {
    std::istringstream input{std::string(text)}; std::string command; input >> command;
    if (command == "b_on_requested" && end(input)) session.requestBOn(now);
    else if (command == "b_on_confirmed") {
        std::string confirmation;
        if (input >> confirmation && end(input)) session.confirmBOn(now, confirmation == "B_ONLY_A_OFF_USB_DISCONNECTED");
        else session.stop("MissingPhysicalConfirmation");
    } else if (command == "capture-proof") {
        std::string run; std::uint64_t generation = 0; std::int64_t stamp = -1; unsigned sends = 1;
        if (input >> run >> generation >> stamp >> sends && end(input)) session.captureProof(run,generation,stamp,sends,now);
        else session.stop("InvalidControl");
    } else if (command == "--q1e-once") {
        vut::q1e_hil::Approval approval; approval.flag = command; std::string gate;
        if (input >> gate >> approval.generation >> approval.run >> approval.token >> approval.physicalConfirmation &&
            end(input) && gate == "Q1E-ONLY") session.send(approval,now);
        else session.stop("MissingOrWrongApproval");
    } else if (command == "stop-capture-unsafe" && end(input)) session.stop("CaptureUnsafe");
    else session.stop("InvalidControl");
}
}

int main(int argc, char** argv) {
    using namespace vut::active_hil;
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "vut_q1e_mapping_hil [--observe] --run-dir PRIVATE_EXISTING_DIRECTORY\n"
            "Default: observe only. No startup send. Research target requires explicit CMake opt-in.\n"
            "Private requests carry individually approved --q1e-once Q1E-ONLY with current generation/run/nonce.\n"
            "Tokens do not replace human approval. Targetless qualified Q1E only; Behavior2 inaccessible.\n"
            "B OFF baseline >=10s; B ON baseline >=30s; post-query observation >=30s. No retry.\n"
            "Finalize/audit capture before shutdown.txt; terminal result retains exclusive ownership.\n";
        return 0;
    }
    std::filesystem::path directory; bool observe = false;
    for (int i=1; i<argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--observe" && !observe) observe = true;
        else if (arg == "--run-dir" && directory.empty() && i+1 < argc) directory = argv[++i];
        else { std::cerr << "InvalidArguments\n"; return 3; }
    }
    if (directory.empty() || !std::filesystem::is_directory(directory)) { std::cerr << "PrivateRunDirectoryRequired\n"; return 3; }
    try {
        const auto trialPath = directory.parent_path()/"q1e-active-trial.lock";
        if (std::filesystem::exists(trialPath)) throw std::runtime_error("TrialAlreadyConsumed");
        DurableJournal owner(directory/"owner.lock");
        const auto run = nonce(), token = nonce(); owner.write(run+"\n");
        DurableJournal log(directory/"runner-private.jsonl");
        const auto started = std::chrono::steady_clock::now();
        auto elapsed = [&] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count(); };
        ResearchHid hid;
        auto audit = [&](const vut::q1e_hil::SendReceipt& receipt) {
            std::ostringstream out;
            out << "{\"kind\":\"send\",\"run\":\"" << run << "\",\"gate\":\"Q1E-ONLY\",\"elapsed_ms\":" << receipt.elapsedMs
                << ",\"generation\":" << receipt.generation << ",\"request_category\":\"ObservedQuery1e\",\"targetless\":true"
                << ",\"request_sha256\":\"" << sha256(receipt.request) << "\",\"request_hex\":\"" << hex(receipt.request)
                << "\",\"send_attempted\":" << receipt.attempted << ",\"transport_returned\":" << receipt.returned
                << ",\"transport_accepted\":" << receipt.accepted << ",\"response_class\":\"" << receipt.responseClass
                << "\",\"response_hex\":\"" << hex(receipt.response) << "\"}\n";
            if (!receipt.attempted) {
                DurableJournal trial(trialPath); trial.write(run+"\n"); // Shared across directories; never removed.
                DurableJournal gate(directory/"Q1E-ONLY.attempt"); gate.write(out.str());
            }
            log.write(out.str());
        };
        vut::q1e_hil::Session session(run,token,hid,audit);
        std::string error;
        if (!hid.open(vut::protocol::kVendorIdVive,vut::protocol::kProductIdUltimateTrackerDongle,true,error)) {
            log.write("{\"kind\":\"runtime\",\"reason\":\"ExclusiveOpenFailed\",\"detail\":\""+error+"\"}\n");
            std::cout << "ExclusiveOpenFailed " << error << "\n"; return 2;
        }
        session.opened(1);
        log.write("{\"kind\":\"runtime\",\"reason\":\"ExclusiveOpenSucceeded\",\"generation\":1}\n");
        std::cout << "ExclusiveOpenSucceeded ObserveOnly\n" << std::flush;
        std::set<std::filesystem::path> processed;
        std::int64_t terminalAt = -1, statusAt = -1;
        auto status = [&](std::int64_t now) {
            std::ostringstream out;
            out << "{\"run\":\"" << run << "\",\"elapsed_ms\":" << now << ",\"generation\":" << session.generation()
                << ",\"observation_epoch\":" << session.observationEpoch() << ",\"candidate\":" << bool(session.candidate())
                << ",\"slot\":" << (session.candidate() ? session.candidate()->slot : -1)
                << ",\"rf\":\"" << (session.candidate() ? hex(session.candidate()->mac) : "") << "\""
                << ",\"b_confirmed\":" << session.confirmed() << ",\"b_on_at_ms\":" << session.bOnAt()
                << ",\"post_at_ms\":" << session.postAt() << ",\"eligible\":" << session.eligible(now)
                << ",\"candidate_before_send\":" << session.candidateBeforeSend()
                << ",\"fresh_identity\":" << session.freshIdentity() << ",\"pose\":" << session.pose()
                << ",\"recovered\":" << session.recovered() << ",\"pairs\":" << session.pairs() << ",\"unpairs\":" << session.unpairs()
                << ",\"rf_reports\":" << session.rfReports() << ",\"rf_transitions\":" << session.rfTransitions()
                << ",\"opaque_rf_record\":\"" << (session.rfRecord() ? hex(*session.rfRecord()) : "")
                << "\",\"opaque_response_record\":\"" << (session.responseRecord() ? hex(*session.responseRecord()) : "")
                << "\",\"response_class\":\"" << session.responseClass() << "\",\"attempts\":" << session.attempts()
                << ",\"reason\":\"" << session.reason() << "\",\"gate_token\":\"" << token << "\",\"epochs\":[";
            bool first = true;
            for (const auto& e : session.epochs()) {
                if (!first) out << ','; first = false;
                out << "{\"id\":" << e.connectionEpochId << ",\"generation\":" << e.transportGeneration
                    << ",\"active\":" << e.active << ",\"progress\":" << unsigned(e.expectedIndex)
                    << ",\"info_complete\":" << e.infoComplete << ",\"initialization_eligible\":" << e.initializationEligible
                    << ",\"pose_observed\":" << e.poseObserved << ",\"reason\":" << static_cast<unsigned>(e.reason) << '}';
            }
            out << "]}\n";
            const auto temporary = directory/"status-private.tmp", target = directory/"status-private.json";
            { std::ofstream file(temporary,std::ios::binary|std::ios::trunc); file << out.str(); file.flush();
              if (!file) throw std::runtime_error("StatusUnavailable"); }
            if (!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("StatusUnavailable");
        };
        try {
            for (;;) {
                if (std::filesystem::exists(directory/"shutdown.txt")) break;
                if (elapsed() >= 900000) session.stop("InfrastructureDeadline");
                if (!session.stopped()) {
                    const auto report = hid.read(50);
                    if (hid.readFailed()) session.stop("DongleTransportLoss");
                    else if (!report.empty()) {
                        log.write("{\"kind\":\"raw\",\"elapsed_ms\":"+std::to_string(elapsed())+",\"report_hex\":\""+hex(report)+"\"}\n");
                        session.observe(report,elapsed());
                    }
                    session.tick(elapsed());
                    if (!session.stopped()) for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                        if (entry.path().extension() != ".request" || processed.contains(entry.path())) continue;
                        processed.insert(entry.path());
                        if (processed.size() > 16 || std::filesystem::file_size(entry.path()) > 2048) { session.stop("ControlLimit"); break; }
                        std::ifstream file(entry.path(),std::ios::binary); std::string text((std::istreambuf_iterator<char>(file)),{});
                        if (text.empty() || file.bad()) { session.stop("ControlUnavailable"); break; }
                        log.write("{\"kind\":\"operator_control\",\"elapsed_ms\":"+std::to_string(elapsed())+",\"command_sha256\":\""+
                            sha256(std::span(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()))+"\"}\n");
                        const auto before = session.attempts(); control(text,session,elapsed());
                        if (session.attempts() != before) session.sendReturnedAt(elapsed());
                        if (session.stopped()) break;
                    }
                }
                if (session.stopped()) {
                    if (terminalAt < 0) {
                        terminalAt = elapsed();
                        log.write("{\"kind\":\"terminal\",\"elapsed_ms\":"+std::to_string(terminalAt)+",\"reason\":\""+session.reason()+"\"}\n");
                        std::cout << "ObservationFinished " << session.reason() << " OwnershipHeld\n" << std::flush;
                    }
                    if (elapsed()-terminalAt >= 180000) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                if (elapsed()-statusAt >= 250) { status(elapsed()); statusAt = elapsed(); }
            }
        } catch (...) {
            session.stop("ProcessException");
            try { status(elapsed()); log.write("{\"kind\":\"terminal\",\"reason\":\"ProcessException\"}\n"); } catch (...) {}
            const auto until = elapsed()+180000;
            while (elapsed()<until && !std::filesystem::exists(directory/"shutdown.txt")) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        hid.close(); log.write("{\"kind\":\"runtime\",\"reason\":\"NormalShutdown\"}\n");
        std::cout << "NormalShutdown\n";
        return session.reason().starts_with("Q1E_") || session.reason() == "NaturalReconnectObserved" ? 0 : 2;
    } catch (...) { std::cerr << "InfrastructureFailureNoRecovery\n"; return 3; }
}
