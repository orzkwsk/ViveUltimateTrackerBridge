#include "mock_consumer.hpp"
namespace c1=monaka::protocol::v2;
bool MockConsumer::receive(std::string_view bytes,std::int64_t now) {
    c1::Envelope value; c1::Error error;
    if(!c1::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size(),value,error)) return false;
    auto* pose=std::get_if<c1::TrackerObservation>(&value);
    auto* state=std::get_if<c1::ObservationDeviceState>(&value);
    if(!pose&&!state) return false;
    const auto& sourceId=pose?pose->source_id:state->source_id;
    const auto& session=pose?pose->session_id:state->session_id;
    auto& source=sources_[sourceId];
    if(source.retired.contains(session)) return false;
    if(source.session!=session) {
        if(!source.session.empty()) source.retired.insert(source.session);
        source.session=session; source.devices.clear();
    }
    const auto& device=pose?pose->device_id:state->device_id;
    auto& cache=source.devices[device];
    const auto revision=pose?pose->coordinate_space.revision:state->coordinate_space.revision;
    if(revision<cache.revision) return false;
    if(revision!=cache.revision) { cache=Cache{}; cache.revision=revision; }
    if(state) {
        if(state->sequence<=cache.stateSequence) return false;
        cache.stateSequence=state->sequence;
        if(state->presence=="absent") { cache.present=false; cache.freshAt=-1; }
        if(state->presence=="present") cache.present=true;
    } else {
        if(pose->sequence<=cache.poseSequence) return false;
        cache.poseSequence=pose->sequence;
        const auto age=pose->sent_at_ns-pose->timestamp_ns;
        cache.freshAt=age<=now?now-age:-1;
    }
    return true;
}
bool MockConsumer::fresh(const std::string& source,const std::string& device,std::int64_t now) const {
    const auto& c=cache(source,device);
    return c.present && c.freshAt>=0 && now>=c.freshAt && now-c.freshAt<500000000;
}
const MockConsumer::Cache& MockConsumer::cache(const std::string& source,const std::string& device) const {
    return sources_.at(source).devices.at(device);
}
