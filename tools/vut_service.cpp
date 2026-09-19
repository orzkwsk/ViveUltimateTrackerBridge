#include "vut/backend.hpp"
#include "vut/observation_adapter.hpp"
#include "vut/observation_publisher.hpp"
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
              << " last_rejected_device=" << (d.lastRejectedDevice.empty() ? "-" : d.lastRejectedDevice)
              << '\n';
}
vut::ObservationContext config(const char* path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read configuration");
    vut::ObservationContext c;
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
        } else throw std::runtime_error("invalid configuration line");
        if (row >> extra) throw std::runtime_error("unexpected configuration field");
    }
    if (c.sourceId.empty() || c.spaces.empty()) throw std::runtime_error("source and per-MAC map configuration required");
    c.sessionId=uuid();
    vut::ViveObservationAdapter validate(c);
    return c;
}
// Exclusive open prevents two service instances using this persistent revision journal.
// Append + FlushFileBuffers precedes every session. Torn/corrupt journals fail closed.
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
        std::cout << "vut_service CONFIG REVISION_JOURNAL [PORT]\nConfiguration: source <persistent-installation-id> and map <full-mac> <map-id> lines.\n"
                     "Default destination 127.0.0.1:29810. Stop/restart after map reset or ambiguous RF gap.\n"
                     "For diagnostics without publication use vut_probe.\n"; return 0;
    }
    if (argc==4 && std::string(argv[1])=="--check-session") {
        const auto c=config(argv[2]); RevisionJournal journal(argv[3]);
        std::cout << c.sourceId << " " << journal.advance() << " " << c.sessionId << '\n';
        return 0; // Reserve/test a session without opening HID.
    }
    if (argc<3 || argc>4) throw std::runtime_error("use --help");
    auto c=config(argv[1]);
    unsigned long port=29810;
    if(argc==4) { std::size_t n=0; port=std::stoul(argv[3],&n); if(n!=std::string(argv[3]).size() || port==0 || port>65535) throw std::runtime_error("invalid port"); }
    RevisionJournal journal(argv[2]);
    vut::LoopbackSender sender(static_cast<std::uint16_t>(port));
    std::signal(SIGINT,stop); std::signal(SIGTERM,stop);
    vut::Backend backend;
    while(run) {
        if(!backend.start()) { std::cerr << backend.lastError() << '\n'; std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
        c.sessionId=uuid(); c.epochNs=nowNs(); c.revision=journal.advance(); c.generation=backend.transportGeneration();
        vut::ViveObservationAdapter adapter(c);
        vut::ObservationPublisher publisher(c.sessionId,[&](auto bytes){return sender.send(bytes);},[&]{return nowNs()-c.epochNs;});
        backend.setEventCallback([&](const auto& event) {
            if(event.kind == vut::BackendEvent::Kind::Diagnostic) {
                std::cerr << "[tracking-diagnostic] device=" << event.device.toString()
                          << " " << event.text << '\n';
            } else if(event.kind == vut::BackendEvent::Kind::Ack &&
                      (event.text.rfind("P61:",0)==0 || event.text.rfind("LP",0)==0 ||
                       event.text.rfind("MS",0)==0 || event.text.rfind("DEC",0)==0)) {
                std::cerr << "[tracking-ack] device=" << event.device.toString()
                          << " " << event.text << '\n';
            }
            if(auto observation=adapter.admit(event)) publisher.enqueue(std::move(*observation));
        });
        std::cerr << "session=" << c.sessionId << " revision=" << c.revision << '\n';
        std::cerr << "source=" << c.sourceId << " configured_maps=" << c.spaces.size() << '\n';
        for(const auto& [deviceId, mapId] : c.spaces)
            std::cerr << "configured_map device=" << deviceId << " map=" << mapId << '\n';
        auto nextAdmissionReport = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while(run && backend.running() && !adapter.restartRequired()) {
            if(!backend.pollOnce()) break;
            publisher.flush();
            const auto now = std::chrono::steady_clock::now();
            if(now >= nextAdmissionReport) {
                printAdmissionDiagnostics(adapter, "[admission]");
                nextAdmissionReport = now + std::chrono::seconds(5);
            }
        }
        backend.setEventCallback({}); backend.stop(); publisher.clear();
        const auto& counts=publisher.counters();
        std::cerr << "sent=" << counts.sent << " send_errors=" << counts.sendErrors << " encode_errors=" << counts.encodeErrors
                  << " dropped=" << counts.dropped << " admission_rejected=" << adapter.rejected() << '\n';
        printAdmissionDiagnostics(adapter, "[admission-final]");
    }
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
