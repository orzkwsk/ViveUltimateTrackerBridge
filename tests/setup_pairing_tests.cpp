#include "vut/setup_console.hpp"
#include "fake_pairing_transport.hpp"
#include <sstream>
#include <iostream>

#define CHECK(x) do { if (!(x)) throw std::runtime_error("CHECK failed: " #x); } while(false)
namespace {
using namespace vut::setup;
using namespace pairing_test;
struct Fixture {
    Transport* fake;
    vut::Backend backend;
    SetupSession session;
    SetupConsole console;
    std::ostringstream output, errors;
    Fixture() : Fixture(std::make_unique<Transport>()) {}
    explicit Fixture(std::unique_ptr<Transport> transport)
        : fake(transport.get()), backend(std::move(transport)), session(backend), console(session) {
        vut::BackendConfig config; config.configureTrackingOnPair = false; config.exclusiveHidAccess = true;
        CHECK(backend.start(config)); session.connected();
    }
    CommandResult command(const std::string& text) { return console.process(text,nowNs(),output,errors); }
};
void commandPacket(const std::vector<std::uint8_t>& p, bool enabled, int slot) {
    CHECK(p.size()==65 && p[1]==vut::protocol::kCmdRequestRfChangeBehavior &&
          p[3]==vut::protocol::kRfBehaviorPairDevice && p[4]==static_cast<std::uint8_t>(enabled));
    for(int i=0;i<5;++i) CHECK(p[5+i]==static_cast<std::uint8_t>(slot==-1 || slot==i));
}
void integratedPairing() {
    Fixture f;
    CHECK(f.fake->writes.empty() && f.fake->openedExclusive);
    CHECK(f.command("pair 2").succeeded);
    commandPacket(f.fake->writes.back(),true,2);
    CHECK(f.session.pairing().result().state==PairingState::Pairing);
    CHECK(!f.command("pair 1").succeeded); // no selector change while active
    CHECK(!f.command("identify").succeeded);
    f.fake->reads.push_back(pairReport(1)); // unrelated selector: still pairing
    CHECK(f.session.pollOnce(nowNs()) && f.session.pairing().active());
    f.fake->reads.push_back(pairReport(2));
    const auto generation=f.backend.transportGeneration();
    CHECK(f.session.pollOnce(nowNs()));
    const auto& result=f.session.pairing().result();
    CHECK(result.state==PairingState::Paired && result.paired && result.cleanupConfirmed);
    commandPacket(f.fake->writes.back(),false,2);
    CHECK(f.backend.running() && f.backend.transportGeneration()==generation && !f.fake->closed);
    CHECK(f.session.devices().entries().at(device().toString()).connected);
    CHECK(!f.session.devices().entries().at(device().toString()).identity);
    CHECK(f.command("refresh").succeeded && f.command("identify").succeeded);
    CHECK(f.session.devices().identifying(nowNs()));
    CHECK(f.fake->opens==1 && f.fake->writes.size()==2);
    CHECK(f.command("pair-status").succeeded);
    CHECK(f.output.str().find("stable_id=UNAVAILABLE assignment=BLOCKED")!=std::string::npos);
}
void timeoutCancelQuit() {
    Fixture f;
    auto start=nowNs();
    CHECK(f.session.pairing().begin(start,-1,std::chrono::seconds(1)));
    f.session.pairing().tick(start+1000000000LL);
    CHECK(f.session.pairing().result().state==PairingState::TimedOut);
    CHECK(!f.backend.pairingActive() && f.backend.running());
    CHECK(f.command("pair all").succeeded && f.command("pair-cancel").succeeded);
    CHECK(f.session.pairing().result().state==PairingState::Cancelling);
    CHECK(f.command("pair 0").succeeded);
    const auto quit=f.command("quit"); CHECK(!quit.keepRunning && quit.succeeded);
    CHECK(!f.backend.pairingActive());
    const auto count=f.fake->writes.size();
    CHECK(f.session.shutdown() && f.fake->writes.size()==count); // idempotent
}
void uncertainAndRetry() {
    Fixture f;
    f.fake->uncertainEnable=true;
    CHECK(!f.command("pair 3").succeeded);
    CHECK(f.session.pairing().result().state==PairingState::Failed && f.session.pairing().result().cleanupConfirmed);
    CHECK(f.fake->writes.size()==2); commandPacket(f.fake->writes.back(),false,3);
    f.fake->uncertainEnable=false;
    CHECK(f.command("pair 4").succeeded);
    f.fake->failDisableWrites=1;
    CHECK(f.command("pair-cancel").succeeded);
    CHECK(f.fake->writes.size()==5); // failed disable followed by bounded retry
    commandPacket(f.fake->writes[3],false,4); commandPacket(f.fake->writes[4],false,4);
    f.fake->throwEnableResponse=true;
    CHECK(!f.command("pair 1").succeeded);
    CHECK(!f.backend.pairingActive() && f.session.pairing().result().cleanupConfirmed);
    CHECK(f.command("refresh").succeeded); // interactive error does not end session
}
void failedCleanup() {
    Fixture f;
    CHECK(f.command("pair").succeeded);
    f.fake->uncertainDisable=true;
    CHECK(!f.command("pair-cancel").succeeded);
    CHECK(f.session.pairing().result().state==PairingState::CleanupFailed);
    CHECK(!f.session.pairing().result().cleanupConfirmed && !f.session.pairing().result().cleanupError.empty());
    CHECK(f.fake->writes.size()==3); // enable and two cleanup attempts
    CHECK(f.output.str().find("UNCONFIRMED")!=std::string::npos);
    CHECK(!f.command("pair").succeeded); // fail closed while cleanup unresolved
    f.fake->uncertainDisable=false;
    CHECK(f.command("pair-cancel").succeeded); // explicit recovery
    CHECK(!f.backend.pairingActive());
    CHECK(f.command("pair").succeeded);
    f.fake->uncertainDisable=true;
    const auto quit=f.command("quit"); CHECK(!quit.keepRunning && !quit.succeeded);
    f.fake->uncertainDisable=false;
    f.backend.stop();
    CHECK(!f.backend.pairingActive());
}
void transportLoss() {
    Fixture f;
    CHECK(f.command("pair 2").succeeded);
    f.fake->failDisableWrites=1; f.fake->failRead=true;
    CHECK(!f.session.pollOnce(nowNs()));
    CHECK(f.fake->closed && f.fake->writes.size()==3); // retry while old handle is open
    CHECK(f.session.pairing().result().state==PairingState::Failed);
    CHECK(f.session.pairing().result().reason.find("HID read failed")!=std::string::npos);
    CHECK(f.session.pairing().result().cleanupConfirmed);
    vut::BackendConfig config; config.configureTrackingOnPair=false;
    CHECK(f.backend.start(config)); f.session.connected();
    CHECK(f.fake->writes.size()==3 && !f.backend.pairingActive()); // reconnect never enables
    CHECK(f.command("pair").succeeded);
    f.fake->uncertainDisable=true; f.fake->failRead=true;
    CHECK(!f.session.pollOnce(nowNs()));
    CHECK(f.session.pairing().result().state==PairingState::CleanupFailed);
    const auto count=f.fake->writes.size();
    f.fake->uncertainDisable=false;
    CHECK(f.backend.start(config)); f.session.connected();
    CHECK(f.fake->writes.size()==count+1); commandPacket(f.fake->writes.back(),false,-1);
    CHECK(!f.backend.pairingActive()); // old confirmed enable cannot survive the new generation
    CHECK(f.session.pollOnce(nowNs()));
    CHECK(f.session.pairing().result().state==PairingState::Failed);
    CHECK(f.session.pairing().result().cleanupConfirmed);
    CHECK(f.session.pairing().result().cleanupError.empty());
    CHECK(f.command("pair 1").succeeded);
    CHECK(f.command("pair-cancel").succeeded);
}
void exceptionShutdown() {
    auto transport=std::make_unique<Transport>(); auto* fake=transport.get();
    vut::Backend backend(std::move(transport)); vut::BackendConfig config; config.configureTrackingOnPair=false;
    CHECK(backend.start(config));
    try {
        SetupSession session(backend); session.connected();
        CHECK(session.pairing().begin(nowNs(),0));
        fake->throwRead=true; (void)session.pollOnce(nowNs());
        CHECK(false);
    } catch(const std::runtime_error&) {}
    CHECK(!backend.pairingActive() && fake->writes.size()==2);
    commandPacket(fake->writes.back(),false,0);
}
void eventAdmission() {
    Fixture f; const auto start=nowNs();
    CHECK(f.session.pairing().begin(start,2));
    auto e=vut::BackendEvent{vut::BackendEvent::Kind::DevicePaired, device(), {}, {}, start,
                            f.backend.transportGeneration()+1};
    f.session.pairing().observe(e); CHECK(!f.session.pairing().result().paired);
    e.transportGeneration=f.backend.transportGeneration(); e.observedMonotonicNs=start-1;
    f.session.pairing().observe(e); CHECK(!f.session.pairing().result().paired);
    e.observedMonotonicNs=start+60000000000LL;
    f.session.pairing().observe(e); CHECK(!f.session.pairing().result().paired);
}
}
int main() try {
    integratedPairing(); timeoutCancelQuit(); uncertainAndRetry(); failedCleanup();
    transportLoss(); exceptionShutdown(); eventAdmission();
    std::cout << "PASS setup pairing/registry/Identify continuity, timeout/cancel/quit/exception, uncertain enable, bounded retry, cleanup reporting, transport recovery and interactive continuation; hardware NOT RUN\n";
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
