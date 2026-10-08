#include "vut/assignment_store.hpp"
#include "vut/backend.hpp"
#include "vut/backend_transport.hpp"
#include "vut/protocol.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

#define CHECK(x) do { if (!(x)) throw std::runtime_error("check failed: " #x); } while (false)
namespace {
using namespace vut::setup;
const auto a = stableIdentity(IdentityKind::HardwareSerial, "factory-A");
const auto b = stableIdentity(IdentityKind::HardwareSerial, "factory-B");
const auto c = stableIdentity(IdentityKind::HardwareSerial, "factory-C");
void fails(const std::function<void()>& action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected);
}
vut::DeviceId device(int slot, unsigned char unique) {
    return {{0x23, static_cast<unsigned char>(0x30 + slot), unique, 0x81, 0xfe, 0xec}, slot};
}
vut::BackendEvent pair(vut::DeviceId d, std::int64_t t, std::uint64_t g=1, bool unpair=false) {
    return {unpair ? vut::BackendEvent::Kind::DeviceUnpaired : vut::BackendEvent::Kind::DevicePaired, d, {}, {}, t, g};
}
vut::BackendEvent pose(vut::DeviceId d, std::int64_t t, float x, unsigned char index,
                       vut::TrackingStatus status=vut::TrackingStatus::Ok, std::uint64_t g=1) {
    vut::TrackerState p;
    p.device = d; p.observedMonotonicNs = t; p.packetIndex = index;
    p.positionMeters.x = x; p.trackingStatus = status;
    return {vut::BackendEvent::Kind::Pose, d, p, {}, t, g};
}
void assignments() {
    Assignments value;
    value.assign(a,"LeftFoot"); value.assign(b,"RightFoot");
    CHECK(value.role(a)=="LeftFoot" && value.role(b)=="RightFoot");
    const auto original = value.entries();
    fails([&]{value.assign(c,"LeftFoot");}); CHECK(value.entries()==original);
    fails([&]{value.assign(a,"UnknownRole");}); CHECK(value.entries()==original);
    value.assign(a,"RightFoot",ConflictAction::Swap);
    CHECK(value.role(a)=="RightFoot" && value.role(b)=="LeftFoot");
    value.assign(c,"RightFoot",ConflictAction::Replace);
    CHECK(value.role(a).empty() && value.role(c)=="RightFoot");
    value.assign(c,"Waist"); CHECK(value.role(c)=="Waist");
    value.swap(a,c); CHECK(value.role(a)=="Waist" && value.role(c).empty());
    value.assign(c,"Waist",ConflictAction::Swap); CHECK(value.role(a).empty());
    value.unassign(c); CHECK(value.role(c).empty()); value.clear(); CHECK(value.entries().empty());
    CHECK(parseStableIdentity(a.key()).key()==a.key());
    CHECK(stableIdentity(IdentityKind::StableUuid,"runtime-verified-uuid").key()=="uuid:runtime-verified-uuid");
    CHECK(stableIdentity(IdentityKind::PersistentIdentifier,"runtime-persistent").key()=="persistent:runtime-persistent");
    fails([]{parseStableIdentity("23:32:03:81:fe:ec");});
    fails([]{parseStableIdentity("slot:2");});
    fails([]{stableIdentity(IdentityKind::HardwareSerial,"");});
    fails([]{stableIdentity(IdentityKind::HardwareSerial,"bad\nidentity");});
}
void reconnect() {
    Assignments assignment; assignment.assign(a,"LeftFoot"); assignment.assign(b,"RightFoot");
    Devices devices; devices.beginSession(1);
    const auto da=device(0,1), db=device(1,2);
    devices.observe(pair(db,100)); devices.observe(pair(da,101));
    CHECK(!devices.entries().at(da.toString()).identity); // RF identity is not promoted
    devices.observeIdentity(da.toString(),a); devices.observeIdentity(db.toString(),b);
    CHECK(assignment.role(*devices.entries().at(db.toString()).identity)=="RightFoot");
    fails([&]{devices.observeIdentity(db.toString(),a);});
    devices.observe(pose(da,200,0,0,vut::TrackingStatus::Lost));
    CHECK(assignment.role(*devices.entries().at(da.toString()).identity)=="LeftFoot");
    devices.observe(pose(da,201,0,1)); CHECK(devices.entries().at(da.toString()).tracking==vut::TrackingStatus::Ok);
    devices.observe(pair(da,300,1,true)); CHECK(!devices.entries().at(da.toString()).connected);
    CHECK(assignment.role(a)=="LeftFoot"); // disconnected assignment retained
    devices.observe(pair(da,301)); CHECK(!devices.entries().at(da.toString()).identity);
    devices.observeIdentity(da.toString(),a); CHECK(assignment.role(*devices.entries().at(da.toString()).identity)=="LeftFoot");
    devices.beginSession(2); CHECK(!devices.entries().at(da.toString()).connected);
    CHECK(devices.entries().at(da.toString()).lastPoseNs==0);
    devices.observe(pair(da,1000,1)); CHECK(!devices.entries().at(da.toString()).connected); // retired generation
    // Different enumeration, RF addresses, slots and session: only VERIFIED
    // injected serial evidence permits restoration. Production lacks that API.
    const auto newB=device(3,9), newA=device(4,10);
    devices.observe(pair(newB,1001,2)); devices.observe(pair(newA,1002,2));
    devices.observeIdentity(newB.toString(),b); devices.observeIdentity(newA.toString(),a);
    CHECK(assignment.role(*devices.entries().at(newA.toString()).identity)=="LeftFoot");
    CHECK(assignment.role(*devices.entries().at(newB.toString()).identity)=="RightFoot");
    const auto replacement=device(4,11);
    devices.observe(pair(replacement,1003,2)); devices.observeIdentity(replacement.toString(),c);
    CHECK(!devices.entries().at(newA.toString()).connected);
    CHECK(assignment.role(c).empty() && assignment.role(a)=="LeftFoot");
    devices.observe(pair(newA,1002,2)); CHECK(!devices.entries().at(newA.toString()).connected); // stale event cannot restore old slot
}
void motion() {
    Devices devices; devices.beginSession(1);
    const auto da=device(0,1), db=device(1,2);
    devices.observe(pair(da,1)); devices.observe(pair(db,1));
    const std::int64_t start = 1000000000;
    devices.beginIdentify(start);
    for (int i=0;i<8;++i) {
        devices.observe(pose(da,start+i*30000000LL,0.025f*i,static_cast<unsigned char>(i)));
        devices.observe(pose(db,start+i*30000000LL,0.0001f*i,static_cast<unsigned char>(i)));
    }
    CHECK(devices.motionCandidates(start+210000000)==std::vector<std::string>{da.toString()});
    CHECK(!devices.entries().at(da.toString()).identity); // Identify never creates stable identity
    CHECK(devices.motionCandidates(start+500000000).empty()); // stale
    CHECK(devices.motionCandidates(start+6000000000LL).empty()); // outside window
    // One large outlier then a stationary tracker cannot qualify.
    devices.beginIdentify(start+1000000000);
    for(int i=0;i<8;++i) devices.observe(pose(da,start+1000000000+i*30000000LL,i==3 ? 2.f:0.f,static_cast<unsigned char>(i)));
    CHECK(devices.motionCandidates(start+1210000000).empty());
    // Duplicate RF packets and tracking loss cannot inflate motion evidence.
    devices.beginIdentify(start+2000000000);
    for(int i=0;i<8;++i) devices.observe(pose(da,start+2000000000+i*30000000LL,0.025f*i,0));
    CHECK(devices.motionCandidates(start+2210000000).empty());
    devices.beginIdentify(start+3000000000);
    for(int i=0;i<8;++i) devices.observe(pose(da,start+3000000000+i*30000000LL,0.025f*i,static_cast<unsigned char>(i),vut::TrackingStatus::Lost));
    CHECK(devices.motionCandidates(start+3210000000).empty());
    // Two moving devices remain ambiguous, requiring another human Identify.
    devices.beginIdentify(start+4000000000);
    for(int i=0;i<8;++i) {
        devices.observe(pose(da,start+4000000000+i*30000000LL,0.025f*i,static_cast<unsigned char>(i)));
        devices.observe(pose(db,start+4000000000+i*30000000LL,0.025f*i,static_cast<unsigned char>(i)));
    }
    CHECK(devices.motionCandidates(start+4210000000).size()==2);
    // Rotation-only post-localization data can identify without inventing position.
    devices.beginIdentify(start+5000000000LL);
    for(int i=0;i<8;++i) {
        auto event=pose(da,start+5000000000LL+i*30000000LL,100.f*i,static_cast<unsigned char>(i),vut::TrackingStatus::RecentlyLost);
        const double halfAngle=0.08*i;
        event.pose->orientationXyzw={0,static_cast<float>(std::sin(halfAngle)),0,static_cast<float>(std::cos(halfAngle))};
        devices.observe(event);
    }
    CHECK(devices.motionCandidates(start+5210000000LL)==std::vector<std::string>{da.toString()});
    CHECK(devices.entries().at(da.toString()).motion.positionDeltaMeters==0);
    // Invalid quaternion and a large reception gap discard accumulated evidence.
    auto invalid=pose(da,start+5220000000LL,0,9); invalid.pose->orientationXyzw={0,0,0,0}; devices.observe(invalid);
    CHECK(devices.motionCandidates(start+5220000000LL).empty());
}
struct Temp {
    std::filesystem::path dir = std::filesystem::temp_directory_path()/
        ("vut-setup-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() { if (!std::filesystem::create_directory(dir)) throw std::runtime_error("test temp directory already exists"); }
    ~Temp() { std::error_code ec; std::filesystem::remove_all(dir,ec); }
};
void write(const std::filesystem::path& path, const std::string& text) { std::ofstream out(path,std::ios::binary); out<<text; }
std::string read(const std::filesystem::path& path) { std::ifstream in(path,std::ios::binary); return {std::istreambuf_iterator<char>(in),{}}; }
void persistence() {
    Temp temp; const auto path=temp.dir/"assignments.json";
    AssignmentStore first(path); first.assignments().assign(a,"LeftFoot"); first.save();
    AssignmentStore restored(path); CHECK(restored.assignments().role(a)=="LeftFoot");
    restored.assignments().assign(b,"RightFoot"); restored.save();
    first.assignments().assign(c,"Waist"); fails([&]{first.save();});
    CHECK(AssignmentStore(path).assignments().role(b)=="RightFoot");
    write(path,R"({"schema_version":1,"future":{"version":7},"assignments":{"serial:factory-A":{"role":"LeftFoot","future_device":"kept"}}})");
    AssignmentStore unknown(path); unknown.assignments().assign(a,"Waist"); unknown.save();
    CHECK(read(path).find("future_device")!=std::string::npos && read(path).find("future")!=std::string::npos);
    for(const auto& bytes : std::vector<std::string>{
        "{broken", R"({"schema_version":2,"assignments":{}})",
        R"({"schema_version":1,"schema_version":1,"assignments":{}})",
        R"({"schema_version":1,"assignments":{"serial:A":{"role":"Unknown"}}})",
        R"({"schema_version":1,"assignments":{"serial:A":{"role":"Waist"},"serial:B":{"role":"Waist"}}})",
        R"({"schema_version":1,"assignments":{"23:32:03:81:fe:ec":{"role":"Waist"}}})",
        R"({"schema_version":1,"assignments":[]})"}) {
        write(path,bytes); fails([&]{AssignmentStore bad(path);}); CHECK(read(path)==bytes);
    }
    write(path,R"({"schema_version":1,"assignments":{}})");
    AssignmentStore locked(path); locked.assignments().assign(a,"Waist");
    const auto before=read(path); const auto lock=std::filesystem::path(path.string()+".lock"); write(lock,"another writer");
    fails([&]{locked.save();}); CHECK(read(path)==before && read(lock)=="another writer");
    std::filesystem::remove(lock); locked.save();
    CHECK(AssignmentStore(path).assignments().role(a)=="Waist");
#ifdef _WIN32
    // Deny delete sharing so atomic replacement actually fails after temp write.
    const auto saved=read(path);
    const auto handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    CHECK(handle!=INVALID_HANDLE_VALUE);
    locked.assignments().assign(a,"Chest");
    bool rejected=false; try { locked.save(); } catch(const std::exception&) { rejected=true; }
    CloseHandle(handle); CHECK(rejected && read(path)==saved);
    locked.save(); CHECK(AssignmentStore(path).assignments().role(a)=="Chest");
#endif
    for(const auto& entry : std::filesystem::directory_iterator(temp.dir)) CHECK(entry.path()==path);
}
#ifdef _WIN32
class Transport : public vut::detail::BackendTransport {
public:
    std::deque<std::vector<std::uint8_t>> reports;
    bool failed = false;
    bool open(std::uint16_t,std::uint16_t,bool,std::string&) override { failed=false; return true; }
    void close() override {}
    std::size_t featureLength() const noexcept override { return 65; }
    bool setFeature(std::span<const std::uint8_t>) override { return true; }
    std::vector<std::uint8_t> getFeature() override { return {}; }
    std::vector<std::uint8_t> read(int) override {
        if(reports.empty()) return {}; auto r=reports.front(); reports.pop_front(); return r;
    }
    bool readFailed() const noexcept override { return failed; }
};
std::vector<std::uint8_t> report(vut::DeviceId d, bool unpair=false) {
    std::vector<std::uint8_t> r(12,0); r[0]=vut::protocol::kResponsePairEvent; r[1]=12; r[5]=unpair;
    std::copy(d.mac.begin(),d.mac.end(),r.begin()+6); return r;
}
void transportIntegration() {
    auto transport=std::make_unique<Transport>(); auto* fake=transport.get();
    vut::Backend backend(std::move(transport)); vut::BackendConfig config; config.configureTrackingOnPair=false;
    Devices devices; Assignments assignment; assignment.assign(a,"Waist");
    backend.setEventCallback([&](const auto& event){devices.observe(event);});
    CHECK(backend.start(config)); devices.beginSession(backend.transportGeneration());
    const auto d=device(0,1); fake->reports.push_back(report(d)); CHECK(backend.pollOnce());
    CHECK(devices.entries().at(d.toString()).connected && !devices.entries().at(d.toString()).identity);
    devices.observeIdentity(d.toString(),a);
    fake->reports.push_back(report(d,true)); CHECK(backend.pollOnce()); CHECK(!devices.entries().at(d.toString()).connected);
    fake->failed=true; CHECK(!backend.pollOnce()); devices.endSession();
    CHECK(backend.start(config)); devices.beginSession(backend.transportGeneration());
    const auto changed=device(2,8); fake->reports.push_back(report(changed)); CHECK(backend.pollOnce());
    CHECK(!devices.entries().at(changed.toString()).identity);
    devices.observeIdentity(changed.toString(),a); CHECK(assignment.role(*devices.entries().at(changed.toString()).identity)=="Waist");
}
#endif
}
int main() try {
    assignments(); std::cout<<"PASS assignment/identity operations and duplicate/unknown rejection\n";
    reconnect(); std::cout<<"PASS injected identity reconnect/order/slot/session/replacement/loss; production identity acquisition NOT IMPLEMENTED\n";
    motion(); std::cout<<"PASS motion freshness/noise/replay/tracking-loss/ambiguity\n";
    persistence(); std::cout<<"PASS persistence/unknown fields/invalid config/atomic lock/stale writer/reload\n";
#ifdef _WIN32
    transportIntegration(); std::cout<<"PASS fake HID transport discovery/unpair/reconnect; hardware NOT RUN\n";
#endif
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
