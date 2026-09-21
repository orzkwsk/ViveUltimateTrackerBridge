#include "vut/backend.hpp"
#include "vut/observation_adapter.hpp"
#include "vut/observation_publisher.hpp"
#include "vut/service_options.hpp"
#include "vut/service_session.hpp"
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
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
void printAdmissionDiagnostics(const vut::ViveObservationAdapter& adapter, const char* prefix) {
    const auto& d = adapter.diagnostics();
    std::cerr << prefix
              << " accepted_pose=" << d.acceptedPose
              << " accepted_state=" << d.acceptedState
              << " rejected_total=" << adapter.rejected()
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
}
int main(int argc,char** argv) try {
    if (argc==2 && std::string(argv[1])=="--help") {
        std::cout << "vut_service CONFIG REVISION_JOURNAL [PORT] [--diagnostics]\nConfiguration: source <persistent-installation-id> and map <full-rf-address> <map-id> lines.\n"
                     "Optional: reconnect_revision strict|preserve (default strict).\n"
                     "preserve means: Preserve coordinate map across tracker reconnect.\n"
                     "Default destination 127.0.0.1:29810. Stop/restart after map reset or ambiguous RF gap.\n"
                     "--diagnostics enables detailed stderr only; it never enables active SLAM queries.\n"
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
    auto configured=config(argv[1]);
    auto c=std::move(configured.observation);
    RevisionJournal journal(argv[2]);
    vut::SessionRevisionCoordinator revisionCoordinator(configured.reconnectRevision);
    const vut::CoordinateMapIdentity coordinateIdentity{c.sourceId, c.spaces};
    auto nextStartCause = vut::SessionStartCause::InitialStart;
    vut::LoopbackSender sender(options.port);
    std::signal(SIGINT,stop); std::signal(SIGTERM,stop);
    vut::Backend backend;
    while(run) {
        if(!backend.start()) { std::cerr << backend.lastError() << '\n'; std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
        const auto session = revisionCoordinator.begin(
            coordinateIdentity, nextStartCause, backend.transportGeneration(),
            [&] { return journal.advance(); }, uuid, nowNs);
        c.sessionId=session.sessionId; c.epochNs=session.epochNs;
        c.revision=session.revision; c.generation=session.transportGeneration;
        vut::ViveObservationAdapter adapter(c);
        vut::ObservationPublisher publisher(c.sessionId,[&](auto bytes){return sender.send(bytes);},[&]{return nowNs()-c.epochNs;});
        backend.setEventCallback([&](const auto& event) {
            if(options.diagnostics && event.kind == vut::BackendEvent::Kind::Diagnostic) {
                std::cerr << "[tracking-diagnostic] device=" << event.device.toString()
                          << " " << event.text << '\n';
            } else if(options.diagnostics && event.kind == vut::BackendEvent::Kind::Ack) {
                std::cerr << "[tracking-ack] device=" << event.device.toString()
                          << " " << event.text << '\n';
            }
            if(auto observation=adapter.admit(event)) publisher.enqueue(std::move(*observation));
        });
        std::cerr << "session=" << c.sessionId << " revision=" << c.revision
                  << " revision_action=" << (session.revisionAdvanced ? "advanced" : "preserved")
                  << " reconnect_revision=" << vut::reconnectRevisionPolicyName(configured.reconnectRevision) << '\n';
        if (options.diagnostics) {
            std::cerr << "source=" << c.sourceId << " configured_maps=" << c.spaces.size() << '\n';
            for(const auto& [deviceId, mapId] : c.spaces)
                std::cerr << "configured_map device=" << deviceId << " map=" << mapId << '\n';
        }
        std::uint64_t lastReportedRejections = 0;
        auto nextAdmissionReport = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while(run && backend.running() && !adapter.restartRequired()) {
            if(!backend.pollOnce()) break;
            publisher.flush();
            const auto now = std::chrono::steady_clock::now();
            if(now >= nextAdmissionReport) {
                if (options.diagnostics) printAdmissionDiagnostics(adapter, "[admission]");
                else if (adapter.rejected() != lastReportedRejections) {
                    const auto& d = adapter.diagnostics();
                    std::cerr << "[admission] rejected=" << adapter.rejected()
                              << " unknown_device=" << d.rejectUnknownDevice
                              << " last_unknown_device=" << (d.lastUnknownDevice.empty() ? "-" : d.lastUnknownDevice) << '\n';
                }
                lastReportedRejections = adapter.rejected();
                nextAdmissionReport = now + std::chrono::seconds(5);
            }
        }
        backend.setEventCallback({}); backend.stop(); publisher.clear();
        nextStartCause = adapter.coordinateContinuityUncertain()
            ? vut::SessionStartCause::ContinuityUncertain
            : vut::SessionStartCause::TransportReconnect;
        const auto& counts=publisher.counters();
        std::cerr << "sent=" << counts.sent << " send_errors=" << counts.sendErrors << " encode_errors=" << counts.encodeErrors
                  << " dropped=" << counts.dropped << " admission_rejected=" << adapter.rejected() << '\n';
        if (options.diagnostics) printAdmissionDiagnostics(adapter, "[admission-final]");
    }
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
