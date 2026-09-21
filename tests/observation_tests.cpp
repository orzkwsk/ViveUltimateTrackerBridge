#include "vut/observation_adapter.hpp"
#include "vut/observation_publisher.hpp"
#include "vut/protocol.hpp"
#include "mock_consumer.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <iterator>
#define CHECK(x) do {if(!(x)) throw std::runtime_error("check failed: " #x);} while(false)
namespace c1=monaka::protocol::v2;
const std::string mac="23:32:42:b7:82:d3";
const std::string sid="00000000-0000-4000-8000-000000000001";
const std::string sid2="00000000-0000-4000-8000-000000000002";
vut::ObservationContext context() { return {"vive-installation-a",sid,1,1,100,{{mac,"map-a"},{"24:32:42:b7:82:d3","map-b"}}}; }
vut::BackendEvent fixture(bool extended=false, int rawStatus=-1) {
    std::ifstream in(std::string(FIXTURE_DIR)+(extended?"/pose39.bin":"/pose37.bin"),std::ios::binary);
    std::vector<std::uint8_t> raw{std::istreambuf_iterator<char>(in),{}};
    CHECK(raw.size() >= 49);
    if (rawStatus >= 0) raw[48] = static_cast<std::uint8_t>(rawStatus);
    const auto env=vut::protocol::parseIncomingEnvelope(raw); CHECK(env);
    const auto p=vut::protocol::parsePose(*env,110); CHECK(p);
    return {vut::BackendEvent::Kind::Pose,p->device,p,{},110,1};
}
void advance(vut::BackendEvent& e,int delta=1) { e.observedMonotonicNs+=10; e.pose->observedMonotonicNs=e.observedMonotonicNs; e.pose->packetIndex+=static_cast<std::uint8_t>(delta); }
std::string encode(const c1::Envelope& e) { std::string s; c1::Error error; CHECK(c1::EncodeEnvelope(e,s,error)); return s; }
int main(int argc,char** argv) try {
    auto c=context(); auto e=fixture();
    CHECK(encode(vut::ViveObservationAdapter::mapPose(*fixture(true).pose,c,0)) == encode(vut::ViveObservationAdapter::mapPose(*e.pose,c,0)));
    if(argc==3 && std::string(argv[1])=="--send") {
        vut::LoopbackSender sender(static_cast<std::uint16_t>(std::stoi(argv[2])));
        vut::ViveObservationAdapter adapter(c);
        vut::ObservationPublisher publisher(sid,[&](auto bytes){return sender.send(bytes);},[]{return 100000LL;});
        for(int i=0;i<1000;++i) { auto v=adapter.admit(e); CHECK(v); publisher.enqueue(*v); publisher.flush(); advance(e); }
        CHECK(publisher.pending()==0); return 0;
    }
    if(argc==3 && std::string(argv[1])=="--emit") {
        std::ofstream out(argv[2]);
        for(int s: {0,1,2,3,4,5,255}) {
            e.pose->trackingStatus=static_cast<vut::TrackingStatus>(s);
            out << encode(vut::ViveObservationAdapter::mapPose(*e.pose,c,s)) << '\n';
        }
        return out?0:1;
    }
    for(int status: {0,1,2,3,4,5,255}) {
        e.pose->trackingStatus=static_cast<vut::TrackingStatus>(status);
        auto p=vut::ViveObservationAdapter::mapPose(*e.pose,c,0);
        CHECK(p.validity.position==(status==2)); CHECK(p.validity.orientation==(status==2));
        CHECK(p.modality==(status==2?"full":"none"));
        CHECK(p.tracking_state==(status==2?"tracked":status==3||status==4?"lost":status==255?"unknown":"initializing"));
        CHECK(!p.battery && !p.linear_velocity && !p.angular_velocity && !p.linear_acceleration);
        CHECK(p.capabilities==std::vector<std::string>({"position","orientation"}));
        CHECK(p.device_id==mac && p.coordinate_space.id=="map-a"); encode(p);
    }
    // Full parser -> admission -> fixed v2 codec round trip for BOTH packet lengths.
    // 0x13 remains NONE at the shared boundary; vendor ovalid is not independent
    // rotation-only evidence. Unknown flagged bytes must never become usable.
    for (bool extended : {false, true}) for (int raw = 0; raw <= 255; ++raw) {
        auto event = fixture(extended, raw);
        vut::ViveObservationAdapter checked(c);
        auto admitted = checked.admit(event); CHECK(admitted);
        c1::Envelope decoded; c1::Error error;
        const auto bytes = encode(*admitted);
        CHECK(c1::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), decoded, error));
        const auto& p = std::get<c1::TrackerObservation>(decoded);
        const bool full = raw == 2 || raw == 0x12;
        CHECK(p.modality == (full ? "full" : "none"));
        CHECK(p.validity.position == full && p.validity.orientation == full);
        CHECK(bool(p.position) == full && bool(p.orientation) == full);
        if (raw > 5 && raw != 0x12 && raw != 0x13) CHECK(p.tracking_state == "unknown");
        if (raw == 0x13) CHECK(p.tracking_state == "lost");
    }
    e=fixture();
    for(float invalid: {std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        auto p=*e.pose; p.positionMeters.x=invalid;
        auto o=vut::ViveObservationAdapter::mapPose(p,c,0); CHECK(!o.position && !o.orientation && o.tracking_state=="lost" && o.modality=="none"); encode(o);
        p.orientationXyzw.x=invalid; o=vut::ViveObservationAdapter::mapPose(p,c,0);
        CHECK(o.tracking_state=="lost" && !o.orientation); encode(o);
    }
    for(float w: {0.0f,0.49f,0.5f,1.5f,1.51f}) {
        auto p=*e.pose; p.orientationXyzw={0,0,0,w};
        auto o=vut::ViveObservationAdapter::mapPose(p,c,0); CHECK(o.validity.orientation==(w>=0.5f&&w<=1.5f)); encode(o);
    }
    vut::ViveObservationAdapter adapter(c);
    auto first=adapter.admit(e); CHECK(first);
    CHECK(std::get<c1::TrackerObservation>(*first).sequence==0);
    CHECK(!adapter.admit(e)); advance(e,0); CHECK(!adapter.admit(e));
    advance(e,-1); CHECK(!adapter.admit(e)); advance(e,2); CHECK(adapter.admit(e));
    CHECK(adapter.diagnostics().acceptedPose==2);
    CHECK(adapter.diagnostics().rejectPoseNonMonotonic==1);
    CHECK(adapter.diagnostics().rejectDuplicateRf==1);
    CHECK(adapter.diagnostics().rejectOldOrAmbiguousRf==1);
    {
        vut::ViveObservationAdapter diagnostic(context());
        auto event = fixture(); CHECK(diagnostic.admit(event));
        advance(event); CHECK(diagnostic.admit(event));
        CHECK(diagnostic.diagnostics().lastRejectedRf == -1);
        CHECK(diagnostic.diagnostics().lastRejectedGapNs == -1);
        advance(event, 0); CHECK(!diagnostic.admit(event));
        auto unknown = event; unknown.device.mac[0] = 0x25; unknown.pose->device = unknown.device;
        CHECK(!diagnostic.admit(unknown));
        const auto rejected = diagnostic.diagnostics();
        CHECK(rejected.lastUnknownDevice == unknown.device.toString());
        advance(event); CHECK(diagnostic.admit(event));
        CHECK(diagnostic.diagnostics().lastRejectedRf == rejected.lastRejectedRf);
        CHECK(diagnostic.diagnostics().lastRejectedGapNs == rejected.lastRejectedGapNs);
        CHECK(diagnostic.diagnostics().lastUnknownDevice == rejected.lastUnknownDevice);
        CHECK(diagnostic.diagnostics().acceptedPose == rejected.acceptedPose + 1);
        CHECK(diagnostic.diagnostics().rejectDuplicateRf == rejected.rejectDuplicateRf);
    }
    // Repeated identical numeric values are fresh when packet receipt advances.
    e=fixture(); e.pose->packetIndex=254;
    vut::ViveObservationAdapter wrap(c);
    CHECK(wrap.admit(e)); advance(e); CHECK(wrap.admit(e)); advance(e);
    auto wrapped=wrap.admit(e); CHECK(wrapped); CHECK(std::get<c1::TrackerObservation>(*wrapped).sequence==2);
    CHECK(std::get<c1::TrackerObservation>(*wrapped).session_id==sid);
    auto old=e; c.sessionId=sid2; c.generation=2; c.epochNs=200; c.revision=2;
    vut::ViveObservationAdapter restarted(c); CHECK(!restarted.admit(old));
    e.transportGeneration=2; e.observedMonotonicNs=e.pose->observedMonotonicNs=210; CHECK(restarted.admit(e));
    c=context(); e=fixture(); vut::ViveObservationAdapter streams(c);
    auto paired=e; paired.kind=vut::BackendEvent::Kind::DevicePaired; paired.pose.reset();
    auto pair=streams.admit(paired); CHECK(pair); CHECK(std::get<c1::ObservationDeviceState>(*pair).sequence==0);
    CHECK(streams.admit(e));
    auto unpaired=paired; unpaired.kind=vut::BackendEvent::Kind::DeviceUnpaired; unpaired.observedMonotonicNs=120;
    auto absent=streams.admit(unpaired); CHECK(absent); advance(e); CHECK(!streams.admit(e));
    paired.observedMonotonicNs=130; CHECK(!streams.admit(paired)); CHECK(streams.restartRequired());
    CHECK(streams.restartReason()==vut::ViveObservationAdapter::RestartReason::ConfiguredDevicePairedAfterAbsent);
    CHECK(!streams.coordinateContinuityUncertain()); advance(e); CHECK(!streams.admit(e));
    auto reuse=fixture(); reuse.device.mac[0]=0x24; reuse.pose->device=reuse.device;
    auto other=streams.admit(reuse); CHECK(other); CHECK(std::get<c1::TrackerObservation>(*other).sequence==0);
    CHECK(std::get<c1::TrackerObservation>(*other).coordinate_space.id=="map-b");
    auto gap=fixture(); vut::ViveObservationAdapter gaps(c); CHECK(gaps.admit(gap));
    gap.observedMonotonicNs=gap.pose->observedMonotonicNs=600000110; ++gap.pose->packetIndex; CHECK(!gaps.admit(gap));
    CHECK(gaps.diagnostics().acceptedPose==1);
    CHECK(gaps.diagnostics().rejectPoseGap==1);
    CHECK(gaps.diagnostics().lastRejectedRf==static_cast<int>(gap.pose->packetIndex));
    CHECK(gaps.diagnostics().lastRejectedGapNs==600000000);
    auto unknown=fixture(); unknown.device.mac[0]=0x25; unknown.pose->device=unknown.device; CHECK(!streams.admit(unknown));
    std::vector<std::string> sent; std::int64_t sendTime=100;
    vut::ObservationPublisher pub(sid,[&](auto b){sent.emplace_back(b);return true;},[&]{return sendTime;},2);
    pub.enqueue(*first); pub.enqueue(*first); CHECK(pub.pending()==1); pub.flush(); CHECK(sent.size()==1);
    MockConsumer mock; CHECK(mock.receive(sent[0],1000));
    CHECK(mock.cache(c.sourceId,mac).freshAt==910); CHECK(mock.fresh(c.sourceId,mac,1000));
    CHECK(!mock.receive(sent[0],2000));
    auto state=std::get<c1::ObservationDeviceState>(*pair); state.sent_at_ns=100;
    CHECK(mock.receive(encode(state),400000000)); CHECK(!mock.fresh(c.sourceId,mac,600000000));
    // Unpair cancels unsent pose, and stale state cannot revive its timestamp.
    pub.enqueue(*first); pub.enqueue(*absent); CHECK(pub.pending()==1); pub.flush();
    CHECK(mock.receive(sent.back(),1001)); CHECK(!mock.fresh(c.sourceId,mac,1001));
    auto next=std::get<c1::TrackerObservation>(*first); next.session_id=next.clock_id=sid2;
    CHECK(mock.receive(encode(next),1002)); CHECK(!mock.receive(sent[0],1003));
    // Distinct installation IDs remain distinct with the same full MAC/slot.
    auto sourceB=next; sourceB.source_id="vive-installation-b";
    CHECK(mock.receive(encode(sourceB),1004)); CHECK(mock.fresh(sourceB.source_id,mac,1004));
    CHECK(!mock.receive("malformed",1005));
    auto broken=*first; std::get<c1::TrackerObservation>(broken).source_id="";
    pub.enqueue(broken); pub.flush(); CHECK(pub.counters().encodeErrors==1);
    pub.enqueue(*first); pub.flush(); CHECK(pub.counters().sent==3);
    vut::ObservationPublisher stopped(sid,[](auto){return false;},[]{return 100;},1);
    for(int i=0;i<10000;++i) { stopped.enqueue(*first); stopped.flush(); }
    CHECK(stopped.counters().sendErrors==10000 && stopped.pending()==0);
    stopped.enqueue(*first); stopped.enqueue(*other); CHECK(stopped.pending()==1 && stopped.counters().dropped==1);
    stopped.enqueue(next); CHECK(stopped.counters().dropped==2); stopped.clear(); CHECK(stopped.pending()==0);
    std::cout << "mapping, admission, publisher and generic consumer tests passed\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
