#include "vut/identity_capture.hpp"
#include "vut/backend.hpp"
#include "vut/protocol.hpp"
#include "fake_pairing_transport.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if(!(x)) throw std::runtime_error("CHECK failed: " #x); } while(false)
int main() try {
    using vut::diagnostic::parseIdentityAck;
    for(auto prefix : {"NADS","NASS","NASI","NAPI","NAV1","NAV"}) {
        auto p=parseIdentityAck(std::string(prefix)+"sample");
        CHECK(p.nameSpace==prefix && p.payload=="sample");
    }
    CHECK(parseIdentityAck("NAV11.0.999.178").payload=="1.0.999.178");
    CHECK(parseIdentityAck("NADS").notes.front()=="empty_payload");
    CHECK(parseIdentityAck("NADS a-1 ").payload==" a-1 "); // no trim or guessed alphabet
    CHECK(parseIdentityAck("NADS"+std::string(300,'x')).payload.size()==300); // no guessed length
    const std::string bytes("NADS\0\xff",6);
    auto parsed=parseIdentityAck(bytes);
    CHECK(parsed.payload.size()==2 && parsed.notes.size()>=2);
    CHECK(vut::diagnostic::hexBytes(parsed.payload)=="00ff");
    CHECK(parseIdentityAck("NXYZanything").nameSpace=="N_OTHER");
    CHECK(parseIdentityAck("ADS").nameSpace=="OTHER");
#ifdef _WIN32
    auto transport=std::make_unique<pairing_test::Transport>();
    auto* fake=transport.get(); vut::Backend backend(std::move(transport));
    vut::BackendConfig cfg; cfg.diagnosticPassiveCapture=true;
    // Deliberately leave configureTrackingOnPair=true and enable SLAM queries:
    // the central passive guard must still prohibit all command exchanges.
    cfg.diagnosticSlamQueries=true;
    std::size_t rawCount=0, ackCount=0;
    cfg.diagnosticReportCallback=[&](auto raw, auto ns, auto gen) { CHECK(!raw.empty() && ns>0 && gen>0); ++rawCount; };
    backend.setEventCallback([&](const auto& e){if(e.kind==vut::BackendEvent::Kind::Ack) ++ackCount;});
    CHECK(backend.start(cfg)); CHECK(!backend.beginPairing(0));
    fake->reads.push_back(pairing_test::pairReport(0)); CHECK(backend.pollOnce());
    auto ack=[&](std::string text) {
        std::vector<std::uint8_t> r(12+text.size(),0);
        r[0]=0x28; const auto d=pairing_test::device();
        std::copy(d.mac.begin(),d.mac.end(),r.begin()+3); r[9]=1; r[10]=1; r[11]=static_cast<std::uint8_t>(text.size());
        std::copy(text.begin(),text.end(),r.begin()+12); fake->reads.push_back(r); CHECK(backend.pollOnce());
    };
    ack("NAZZ"); ack("NADSsynthetic-A"); ack("NASSsynthetic-A");
    auto unpair=pairing_test::pairReport(0); unpair[5]=1;
    fake->reads.push_back(unpair); CHECK(backend.pollOnce());
    CHECK(rawCount==5 && ackCount==3 && fake->writes.empty() && fake->featureReads==0);
    backend.stop(); CHECK(fake->writes.empty());
    CHECK(backend.start(cfg)); backend.stop(); CHECK(fake->writes.empty() && fake->featureReads==0);
#endif
    std::cout << "PASS diagnostic parser byte preservation; passive pair/NAZZ/unpair/reconnect has no feature writes\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
