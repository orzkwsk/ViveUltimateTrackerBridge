// Offline recorded vendor bytes -> actual decoder/adapter -> pinned codec.
// Never creates a Backend/HID/dongle/service or normal process resource.
#include "vut/observation_adapter.hpp"
#include "vut/protocol.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
namespace p=monaka::protocol::v2;
void require(bool ok,const char*reason){if(!ok)throw std::runtime_error(reason);}
int main(int argc,char**argv)try{
 require(argc==4,"usage: vut_p0_adapter_replay OUTPUT SESSION_UUID REVISION");
 const std::string session=argv[2];const auto revision=static_cast<uint32_t>(std::stoul(argv[3]));
 std::ifstream input(std::string(FIXTURE_DIR)+"/pose37.bin",std::ios::binary);std::vector<uint8_t> raw{std::istreambuf_iterator<char>(input),{}};require(raw.size()>=49,"vendor fixture missing");
 const auto initial=vut::protocol::parseIncomingEnvelope(raw);require(bool(initial),"vendor envelope");const auto decoded=vut::protocol::parsePose(*initial,1000);require(bool(decoded),"vendor Pose");
 vut::ObservationContext context{"p0-synthetic-altra",session,1,revision,1000,{{decoded->device.toString(),"p0-altra-map"}}};
 vut::ViveObservationAdapter adapter(context);std::ofstream out(argv[1]);require(bool(out),"fixture output");
 for(int i=0;i<100;++i){
  raw[48]=static_cast<uint8_t>(i<40||i>=60?0x02:i<50?0x04:0x03);
  const auto envelope=vut::protocol::parseIncomingEnvelope(raw);require(bool(envelope),"vendor envelope");
  const auto time=1000+static_cast<int64_t>(i+1)*20'000'000;auto pose=vut::protocol::parsePose(*envelope,time);require(bool(pose),"vendor Pose");pose->packetIndex=static_cast<uint8_t>(i);
  vut::BackendEvent event{vut::BackendEvent::Kind::Pose,pose->device,pose,{},time,1};auto observation=adapter.admit(event);require(bool(observation),"actual adapter rejected synthetic sample");
  std::string wire;p::Error codec;require(p::EncodeEnvelope(*observation,wire,codec),codec.message.c_str());out<<"{\"received_at_ns\":"<<time<<",\"envelope\":"<<wire<<"}\n";
 }
 require(bool(out),"fixture write failed");std::cerr<<"actual_adapter=Altra source=p0-synthetic-altra device="<<decoded->device.toString()<<" frames=100 physical_hil=NOT_RUN\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
