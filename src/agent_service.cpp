#include "vut/agent_service.hpp"
#include "vut/agent_lifecycle.hpp"
#include "vut/agent_tray.hpp"
#include "vut/agent_ipc.hpp"
#include "vut/agent_control.hpp"
#include "vut/service_runtime.hpp"
#include "vut/service_options.hpp"
#include "vut/service_session.hpp"
#include <windows.h>
#include <objbase.h>
#include <array>
#include <atomic>
#include <bitset>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <thread>
#include <utility>

namespace {
std::atomic_bool run{true};
void stop(int) { run = false; }
std::int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::string uuid() {
    GUID id{};
    if (FAILED(CoCreateGuid(&id))) throw std::runtime_error("UUID generation failed");
    char result[37];
    snprintf(result, sizeof(result), "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        id.Data1, id.Data2, id.Data3, id.Data4[0],id.Data4[1],id.Data4[2],id.Data4[3],id.Data4[4],id.Data4[5],id.Data4[6],id.Data4[7]);
    return result;
}
void printAdmissionDiagnostics(const vut::ServiceRuntimeStatistics& statistics, const char* prefix) {
    const auto& d = statistics.admission;
    std::cerr << prefix
              << " accepted_pose=" << d.acceptedPose
              << " accepted_state=" << d.acceptedState
              << " rejected_total=" << statistics.rejected
              << " generation=" << d.rejectGeneration
              << " before_epoch=" << d.rejectBeforeEpoch
              << " unknown_device=" << d.rejectUnknownDevice
              << " missing_pose=" << d.rejectMissingPose
              << " pose_device_mismatch=" << d.rejectPoseDeviceMismatch
              << " absent=" << d.rejectAbsent
              << " pose_timestamp_mismatch=" << d.rejectPoseTimestampMismatch
              << " pose_nonmonotonic=" << d.rejectPoseNonMonotonic
              << " duplicate_rf=" << d.rejectDuplicateRf
              << " old_ambiguous_rf=" << d.rejectOldOrAmbiguousRf
              << " pose_gap=" << d.rejectPoseGap
              << " pose_sequence_exhausted=" << d.rejectPoseSequenceExhausted
              << " state_sequence_exhausted=" << d.rejectStateSequenceExhausted
              << " state_nonmonotonic=" << d.rejectStateNonMonotonic
              << " repair_after_absent=" << d.rejectRepairAfterAbsent
              << " last_accepted_rf=" << d.lastAcceptedRf
              << " last_rejected_rf=" << d.lastRejectedRf
              << " last_rejected_gap_ms="
              << (d.lastRejectedGapNs < 0 ? -1.0 : static_cast<double>(d.lastRejectedGapNs) / 1000000.0)
              << " last_unknown_device=" << (d.lastUnknownDevice.empty() ? "-" : d.lastUnknownDevice)
              << '\n';
}
class UnknownStatusEvidence {
    struct DeviceEvidence {
        std::bitset<256> observedRaw;
        std::uint64_t samplesSinceReport = 0;
        std::uint64_t rawChangesSinceReport = 0;
        std::uint8_t lastRaw = 0xff;
        bool hasLast = false;
        std::string device;
        std::chrono::steady_clock::time_point nextSummary{};
    };
    // Five protocol selectors plus one bounded bucket for an invalid slot.
    std::array<DeviceEvidence, 6> slots_;
public:
    void observe(const vut::BackendEvent& event) {
        if (event.kind != vut::BackendEvent::Kind::Pose || !event.pose ||
            event.pose->trackingStatus != vut::TrackingStatus::Unknown) return;
        const auto& pose = *event.pose;
        const auto device = event.device.toString();
        const std::size_t index = event.device.slot >= 0 && event.device.slot < 5
            ? static_cast<std::size_t>(event.device.slot) : 5;
        auto& evidence = slots_[index];
        if (evidence.device != device) {
            evidence = {};
            evidence.device = device;
        }
        const auto now = std::chrono::steady_clock::now();
        const bool newRaw = !evidence.observedRaw.test(pose.trackingStatusRaw);
        evidence.observedRaw.set(pose.trackingStatusRaw);
        ++evidence.samplesSinceReport;
        if (evidence.hasLast && evidence.lastRaw != pose.trackingStatusRaw)
            ++evidence.rawChangesSinceReport;
        evidence.hasLast = true;
        evidence.lastRaw = pose.trackingStatusRaw;
        if (!newRaw && now < evidence.nextSummary) return;

        std::cerr << "[tracking-status-unknown] device=" << device
                  << " slot=" << event.device.slot
                  << " status_raw=0x" << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned>(pose.trackingStatusRaw)
                  << std::dec << std::setfill(' ')
                  << " status=" << vut::trackingStatusName(pose.trackingStatus)
                  << " rf=" << static_cast<unsigned>(pose.packetIndex)
                  << " samples=" << evidence.samplesSinceReport
                  << " raw_changes=" << evidence.rawChangesSinceReport << '\n';
        evidence.samplesSinceReport = 0;
        evidence.rawChangesSinceReport = 0;
        evidence.nextSummary = now + std::chrono::seconds(5);
    }
};
struct ServiceConfiguration {
    vut::ObservationContext observation;
    vut::ReconnectRevisionPolicy reconnectRevision = vut::ReconnectRevisionPolicy::Strict;
};
ServiceConfiguration config(const char* path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read configuration");
    ServiceConfiguration result;
    auto& c = result.observation;
    bool hasReconnectRevision = false;
    std::string line;
    while (std::getline(in,line)) {
        if (line.empty() || line[0]=='#') continue;
        std::istringstream row(line); std::string key,a,b,extra;
        row >> key >> a;
        if (key=="source" && c.sourceId.empty()) c.sourceId=a;
        else if (key=="map" && (row >> b)) {
            if (a.size()!=17) throw std::runtime_error("map key must be lowercase full MAC");
            for (std::size_t i=0; i<a.size(); ++i)
                if ((i%3==2) ? a[i]!=':' : !(a[i]>='0'&&a[i]<='9') && !(a[i]>='a'&&a[i]<='f'))
                    throw std::runtime_error("invalid MAC");
            if (!c.spaces.emplace(a,b).second) throw std::runtime_error("duplicate map MAC");
        } else if (key=="reconnect_revision" && !hasReconnectRevision) {
            result.reconnectRevision = vut::parseReconnectRevisionPolicy(a);
            hasReconnectRevision = true;
        } else throw std::runtime_error("invalid configuration line");
        if (row >> extra) throw std::runtime_error("unexpected configuration field");
    }
    if (c.sourceId.empty() || c.spaces.empty()) throw std::runtime_error("source and per-MAC map configuration required");
    c.sessionId=uuid();
    vut::ViveObservationAdapter validate(c);
    return result;
}
// Exclusive open prevents two service instances using this persistent revision journal.
// Append + FlushFileBuffers precedes every newly reserved coordinate revision.
// Preserved reconnects do not modify the journal. Torn/corrupt journals fail closed.
class RevisionJournal {
    HANDLE file_ = INVALID_HANDLE_VALUE;
    std::uint32_t revision_ = 0;
public:
    explicit RevisionJournal(const char* path) {
        file_=CreateFileA(path,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (file_==INVALID_HANDLE_VALUE) throw std::runtime_error("revision journal is locked or unavailable");
        try {
            LARGE_INTEGER size{};
            if (!GetFileSizeEx(file_,&size) || size.QuadPart>16*1024*1024) throw std::runtime_error("invalid journal size");
            std::string bytes(static_cast<std::size_t>(size.QuadPart),'\0'); DWORD count=0;
            if (!ReadFile(file_,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr) || count!=bytes.size()) throw std::runtime_error("journal read failed");
            if (!bytes.empty() && bytes.back()!='\n') throw std::runtime_error("torn revision journal");
            std::istringstream rows(bytes); std::string line;
            while (std::getline(rows,line)) {
                std::size_t consumed=0; auto v=std::stoull(line,&consumed);
                if (consumed!=line.size() || v!=static_cast<std::uint64_t>(revision_)+1 || v>UINT32_MAX)
                    throw std::runtime_error("corrupt revision journal");
                revision_=static_cast<std::uint32_t>(v);
            }
        } catch (...) { CloseHandle(file_); file_=INVALID_HANDLE_VALUE; throw; }
    }
    ~RevisionJournal() { if(file_!=INVALID_HANDLE_VALUE) CloseHandle(file_); }
    std::uint32_t advance() {
        if (revision_==UINT32_MAX) throw std::runtime_error("revision exhausted; provision new map identity");
        auto bytes=std::to_string(revision_+1)+"\n"; DWORD count=0;
        if (!WriteFile(file_,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr) || count!=bytes.size() || !FlushFileBuffers(file_))
            throw std::runtime_error("cannot persist revision");
        return ++revision_;
    }
};
BOOL WINAPI consoleStop(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
    run = false;
    return TRUE;
}
class ConsoleStop {
public:
    ConsoleStop() {
        run = true;
        if (!SetConsoleCtrlHandler(consoleStop, TRUE)) throw std::runtime_error("console stop handler unavailable");
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
    }
    ~ConsoleStop() {
        SetConsoleCtrlHandler(consoleStop, FALSE);
        std::signal(SIGINT, SIG_DFL);
        std::signal(SIGTERM, SIG_DFL);
    }
};
// Reverse member destruction keeps every dependency alive until Runtime is gone.
// Agent destroys this aggregate before closing its child job and instance mutex.
class TrackingOwner final : public vut::AgentRuntime {
    ServiceConfiguration configured_;
    RevisionJournal journal_;
    vut::LoopbackSender sender_;
    UnknownStatusEvidence unknownStatusEvidence_;
    bool diagnostics_;
    vut::ServiceRuntime runtime_;
    std::uint64_t lastReportedRejections_ = 0;
    std::chrono::steady_clock::time_point nextAdmissionReport_;
    bool closed_ = false;
    vut::ServiceRuntimeConfig runtimeConfig() const {
        vut::ServiceRuntimeConfig result;
        result.observation = configured_.observation;
        result.reconnectRevision = configured_.reconnectRevision;
        result.diagnostics = diagnostics_;
        return result;
    }
    vut::ServiceRuntimeDependencies dependencies() {
        vut::ServiceRuntimeDependencies result;
        result.clock = nowNs;
        result.advanceRevision = [this] { return journal_.advance(); };
        result.makeSessionId = uuid;
        result.send = [this](auto bytes) { return sender_.send(bytes); };
        return result;
    }
    void reportFault() const {
        const auto snapshot = runtime_.snapshot();
        if (snapshot->state != vut::ServiceRuntimeState::Faulted) return;
        std::cerr << "[service-control-fault] code=" << static_cast<int>(snapshot->outcome.code)
                  << " reason=" << static_cast<int>(snapshot->outcome.reason)
                  << " stage=" << static_cast<int>(snapshot->failure)
                  << " history_full=" << (snapshot->outcome.reason == vut::control::Reason::HistoryFull) << '\n';
        throw std::runtime_error("passive service runtime fault; no automatic control-model reset");
    }
public:
    TrackingOwner(const char* configPath, const char* journalPath, std::uint16_t port, bool diagnostics)
        : configured_(config(configPath)), journal_(journalPath), sender_(port), diagnostics_(diagnostics),
          runtime_(vut::Backend{}, runtimeConfig(), dependencies()) {}
    std::string controlRequest(const std::string& bytes) { return vut::agentControlRequest(runtime_, bytes); }
    bool start() override {
        auto& runtime = runtime_;
        if (!runtime.start().ok()) {
            reportFault();
            std::cerr << runtime.snapshot()->backendError << '\n';
            return false;
        }
        const auto session = *runtime.snapshot()->observationSession;
        std::cerr << "session=" << session.sessionId << " revision=" << session.revision
                  << " revision_action=" << (session.revisionAdvanced ? "advanced" : "preserved")
                  << " reconnect_revision=" << vut::reconnectRevisionPolicyName(configured_.reconnectRevision) << '\n';
        if (diagnostics_) {
            const auto& c = configured_.observation;
            std::cerr << "source=" << c.sourceId << " configured_maps=" << c.spaces.size() << '\n';
            for (const auto& [deviceId, mapId] : c.spaces)
                std::cerr << "configured_map device=" << deviceId << " map=" << mapId << '\n';
        }
        nextAdmissionReport_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        return true;
    }
    bool pollOnce() override {
        const bool polled = runtime_.pollOnce();
        for (const auto& event : runtime_.takeDiagnostics()) {
            unknownStatusEvidence_.observe(event);
            if (event.kind == vut::BackendEvent::Kind::Diagnostic)
                std::cerr << "[tracking-diagnostic] device=" << event.device.toString() << " " << event.text << '\n';
            else if (event.kind == vut::BackendEvent::Kind::Ack)
                std::cerr << "[tracking-ack] device=" << event.device.toString() << " " << event.text << '\n';
        }
        if (!polled) { reportFault(); return false; }
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextAdmissionReport_) {
            const auto statistics = runtime_.statistics();
            if (diagnostics_) printAdmissionDiagnostics(statistics, "[admission]");
            else if (statistics.rejected != lastReportedRejections_) {
                const auto& d = statistics.admission;
                std::cerr << "[admission] rejected=" << statistics.rejected
                          << " unknown_device=" << d.rejectUnknownDevice
                          << " last_unknown_device=" << (d.lastUnknownDevice.empty() ? "-" : d.lastUnknownDevice) << '\n';
            }
            lastReportedRejections_ = statistics.rejected;
            nextAdmissionReport_ = now + std::chrono::seconds(5);
        }
        return true;
    }
    bool shutdown() noexcept override {
        if (closed_) return true;
        closed_ = true;
        try {
            runtime_.shutdown();
            const auto statistics = runtime_.statistics();
            const auto& counts = statistics.publication;
            std::cerr << "sent=" << counts.sent << " send_errors=" << counts.sendErrors << " encode_errors=" << counts.encodeErrors
                      << " dropped=" << counts.dropped << " admission_rejected=" << statistics.rejected << '\n';
            if (diagnostics_) printAdmissionDiagnostics(statistics, "[admission-final]");
            reportFault();
            return true;
        } catch (...) { return false; }
    }
};
}
int vut::runAgentService(int argc,char** argv) try {
    if (argc==2 && std::string(argv[1])=="--help") {
        std::cout << "vut_service CONFIG REVISION_JOURNAL [PORT] [--diagnostics] [--tray] [--gui=PATH]\nConfiguration: source <persistent-installation-id> and map <full-rf-address> <map-id> lines.\n"
                     "Optional: reconnect_revision strict|preserve (default strict).\n"
                     "preserve means: Preserve coordinate map across tracker reconnect.\n"
                     "Default destination 127.0.0.1:29810. Stop/restart after map reset or ambiguous RF gap.\n"
                     "--diagnostics enables detailed stderr only; it never enables active SLAM queries.\n"
                     "ObserveOnly: no automatic vendor commands; transport loss/session restart exits unavailable.\n"
                     "For diagnostics without publication use vut_probe.\n"; return 0;
    }
    if (argc==4 && std::string(argv[1])=="--check-session") {
        const auto configured=config(argv[2]); RevisionJournal journal(argv[3]);
        const auto& c = configured.observation;
        std::cout << c.sourceId << " " << journal.advance() << " " << c.sessionId << " "
                  << vut::reconnectRevisionPolicyName(configured.reconnectRevision) << '\n';
        return 0; // Reserve/test a session without opening HID.
    }
    if (argc<3) throw std::runtime_error("use --help");
    std::vector<std::string_view> arguments;
    for (int i = 3; i < argc; ++i) arguments.emplace_back(argv[i]);
    const auto options = vut::parseServiceOptions(arguments);
    ConsoleStop console;
    // Reverse destruction: tracking/owned GUI, IPC worker, then tray worker.
    std::unique_ptr<AgentTray> tray;
    std::unique_ptr<AgentIpc> ipc;
    TrackingOwner* tracking = nullptr;
    AgentLifecycle agent;
    if (!agent.start({argv[1], argv[2], options.port}, [&] {
            auto owned = std::make_unique<TrackingOwner>(argv[1], argv[2], options.port, options.diagnostics);
            tracking = owned.get();
            return owned;
        })) throw std::runtime_error(agent.error());
    if (options.tray) { ipc = std::make_unique<AgentIpc>(); tray = std::make_unique<AgentTray>(); }
    while(run) {
        if (tray) {
            if (!tray->responsive() || !ipc->healthy()) throw std::runtime_error("Agent tray/IPC unavailable; stopping owned runtime");
            const auto action = tray->takeAction();
            if (action == AgentTrayAction::Exit) { agent.requestStop(); break; }
            if (action == AgentTrayAction::Open) {
                try {
                    if (agent.hasOwnedChild()) {
                        if (!agent.showOwnedChild()) std::cerr << "owned GUI is starting; retry Open\n";
                    } else if (!options.guiExecutable.empty()) {
                        ipc->allowOwnedChild(0);
                        const auto pid = agent.spawnOwnedChild(options.guiExecutable,
                            {L"--agent-pipe", ipc->name(), L"--agent-pid", std::to_wstring(GetCurrentProcessId())}, AgentChildVisibility::Normal);
                        ipc->allowOwnedChild(pid);
                    } else std::cerr << "GUI not configured; use --gui=PATH with --tray\n";
                } catch (const std::exception& error) { std::cerr << "GUI open failed: " << error.what() << '\n'; }
            }
            if (agent.state() != AgentState::Running) break;
            ipc->pump([&](const std::string& bytes) { return tracking->controlRequest(bytes); });
        }
        if (!agent.pollOnce()) break;
    }
    if (!run) agent.requestStop();
    agent.shutdown();
    if (agent.state() == AgentState::Faulted) throw std::runtime_error(agent.error());
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
