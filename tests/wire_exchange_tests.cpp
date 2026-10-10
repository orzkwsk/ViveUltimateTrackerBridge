#include "vut/backend.hpp"
#include "vut/backend_transport.hpp"
#include "vut/pairing.hpp"
#include "fake_pairing_transport.hpp"

#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>

#define CHECK(x) do { if (!(x)) throw std::runtime_error("CHECK failed: " #x); } while (false)

namespace {
using namespace vut;
using namespace vut::setup;
bool corruptWireFixture = false; // explicit negative Release-test gate only
enum class Reply { Match, Empty, Short, Wrong, LowDeclaration, Truncated, Throw };
struct Trace {
    std::vector<std::vector<std::uint8_t>> writes;
    unsigned gets = 0, closes = 0;
};
class Transport final : public detail::BackendTransport {
public:
    std::shared_ptr<Trace> trace = std::make_shared<Trace>();
    Reply reply = Reply::Match;
    std::deque<std::vector<std::uint8_t>> responses, reads;
    bool rejectNextWrite = false, throwNextWrite = false, throwClose = false, lost = false;
    std::uint8_t command = 0;
    bool open(std::uint16_t, std::uint16_t, bool, std::string&) override { lost = false; return true; }
    void close() override { ++trace->closes; if (throwClose) throw std::runtime_error("PRIVATE-CLOSE"); }
    std::size_t featureLength() const noexcept override { return 65; }
    bool setFeature(std::span<const std::uint8_t> p) override {
        trace->writes.emplace_back(p.begin(), p.end()); command = p[1];
        if (throwNextWrite) { throwNextWrite = false; throw std::runtime_error("PRIVATE-WRITE"); }
        if (rejectNextWrite) { rejectNextWrite = false; return false; }
        return true;
    }
    std::vector<std::uint8_t> getFeature() override {
        ++trace->gets;
        if (!responses.empty()) { auto r = responses.front(); responses.pop_front(); return r; }
        switch (reply) {
        case Reply::Empty: return {};
        case Reply::Short: return {0, command, 4, 0};
        case Reply::Wrong: return {0, static_cast<std::uint8_t>(command ^ 1), 4, 0, 0};
        case Reply::LowDeclaration: return {0, command, 3, 0, 0};
        case Reply::Truncated: return {0, command, 255, 0, 0};
        case Reply::Throw: throw std::runtime_error("PRIVATE-RESPONSE");
        default: return {0, command, 4, 0, 0};
        }
    }
    std::vector<std::uint8_t> read(int) override {
        if (reads.empty()) return {};
        auto r = reads.front(); reads.pop_front(); return r;
    }
    bool readFailed() const noexcept override { return lost; }
};
struct Fixture {
    Transport* fake;
    Backend backend;
    Fixture() : Fixture(std::make_unique<Transport>()) {}
    explicit Fixture(std::unique_ptr<Transport> t) : fake(t.get()), backend(std::move(t)) {
        BackendConfig config; config.configureTrackingOnPair = false;
        CHECK(backend.start(config));
    }
};
void unverified(const FeatureExchangeEvidence& e) { CHECK(e.effect == SemanticEffect::Unverified); }
void expectTransportException(const std::function<void()>& action) {
    bool threw = false;
    try { action(); } catch (const std::runtime_error& e) {
        threw = true;
        CHECK(std::string(e.what()).find("PRIVATE") == std::string::npos);
    }
    CHECK(threw);
}
void pairingPacket(const std::vector<std::uint8_t>& p, bool enable, int slot) {
    std::vector<std::uint8_t> expected(65, 0);
    expected[1] = 0x1d; expected[2] = 9; expected[3] = 0; expected[4] = enable;
    for (int i = 0; i < 5; ++i) expected[5+i] = slot == -1 || slot == i;
    CHECK(p == expected);
}
void writeAndResponses() {
    {
        Fixture f; f.fake->rejectNextWrite = true;
        CHECK(!f.backend.beginPairing(2));
        const auto e = f.backend.pairingEnableExchange();
        CHECK(e.write == FeatureWriteState::Failed && e.response == FeatureResponseState::NotObserved);
        CHECK(e.error == FeatureExchangeError::WriteRejected && e.responseAttempts == 0 && f.fake->trace->gets == 0);
        CHECK(!f.backend.pairingActive());
        CHECK(f.backend.pairingCleanupEvidence().state == CleanupExchangeState::NotNeeded);
        unverified(e); f.backend.stop(); CHECK(f.fake->trace->writes.size() == 1);
    }
    for (const auto mode : {Reply::Empty, Reply::Short, Reply::Wrong, Reply::LowDeclaration, Reply::Truncated}) {
        Fixture f; f.fake->reply = mode;
        CHECK(!f.backend.beginPairing(1));
        const auto e = f.backend.pairingEnableExchange();
        CHECK(e.write == FeatureWriteState::AcceptedByTransportApi && f.backend.pairingActive());
        unverified(e);
        if (mode == Reply::Empty) {
            CHECK(e.response == FeatureResponseState::NotObserved && e.emptyResponses == 10);
            CHECK(e.responseAttempts == 10 && e.error == FeatureExchangeError::NoResponse);
        } else if (mode == Reply::Wrong) {
            CHECK(e.response == FeatureResponseState::WrongCommandOnly && e.wrongCommandResponses == 10);
            CHECK(e.responseAttempts == 10 && e.error == FeatureExchangeError::WrongCommandOnly);
        } else {
            CHECK(e.response == FeatureResponseState::Malformed && e.error == FeatureExchangeError::MalformedResponse);
            CHECK(e.matchingMalformedResponses == (mode == Reply::Short ? 10 : 1));
            CHECK(e.responseAttempts == (mode == Reply::Short ? 10 : 1));
            CHECK(e.framingError == (mode == Reply::Short ? FeatureFramingError::TooShort :
                mode == Reply::LowDeclaration ? FeatureFramingError::InvalidDeclaredLength : FeatureFramingError::DeclaredLengthExceedsBuffer));
        }
        CHECK(f.backend.lastFailedFeatureExchange());
        CHECK(f.backend.lastError().find("PRIVATE") == std::string::npos);
        f.fake->reply = Reply::Match; CHECK(f.backend.endPairing());
        CHECK(f.backend.pairingCleanupEvidence().effect == SemanticEffect::Unverified);
    }
    {
        Fixture f;
        f.fake->responses = {{0, 0x18, 4, 0, 0}, {0, 0x1d, 7, 0, 0, 1, 2, 3}};
        CHECK(f.backend.beginPairing(0));
        const auto e = f.backend.pairingEnableExchange();
        CHECK(e.matched() && e.responseAttempts == 2 && e.wrongCommandResponses == 1);
        unverified(e);
    }
    {
        Fixture f; f.fake->reply = Reply::Empty;
        f.fake->responses = {{0}, {0, 0x18, 4, 0, 0}};
        CHECK(!f.backend.beginPairing());
        const auto e = f.backend.pairingEnableExchange();
        CHECK(e.response == FeatureResponseState::Malformed && e.malformedResponses == 1);
        CHECK(e.wrongCommandResponses == 1 && e.emptyResponses == 8 && e.responseAttempts == 10);
        CHECK(e.lastObservation == FeatureResponseObservation::Empty);
        f.fake->reply = Reply::Match;
    }
    // declared length excludes HIDAPI's leading report-ID byte. Boundary 65
    // requires 66 actual bytes, and cannot be clipped into a successful match.
    for (auto declared : {0, 1, 2, 3, 65, 255}) {
        Fixture f; auto response = std::vector<std::uint8_t>(65, 0);
        response[1] = 0x1d; response[2] = static_cast<std::uint8_t>(declared);
        f.fake->responses.push_back(response);
        CHECK(!f.backend.beginPairing());
        CHECK(f.backend.pairingEnableExchange().response == FeatureResponseState::Malformed);
    }
}
void disableMatrixAndExceptions() {
    for (const auto mode : {Reply::Empty, Reply::Short, Reply::Wrong, Reply::LowDeclaration, Reply::Truncated}) {
        Fixture f; CHECK(f.backend.beginPairing(3)); f.fake->reply = mode;
        CHECK(!f.backend.endPairing());
        CHECK(f.backend.pairingCleanupEvidence().state == CleanupExchangeState::Failed);
        CHECK(f.backend.pairingActive() && f.backend.pairingCleanupEvidence().effect == SemanticEffect::Unverified);
        f.fake->reply = Reply::Match; CHECK(f.backend.endPairing());
        CHECK(f.backend.pairingCleanupEvidence().state == CleanupExchangeState::Matched);
        unverified(f.backend.pairingCleanupEvidence().disable);
    }
    {
        Fixture f; CHECK(f.backend.beginPairing(4)); f.fake->rejectNextWrite = true;
        CHECK(!f.backend.endPairing());
        CHECK(f.backend.pairingCleanupEvidence().disable.write == FeatureWriteState::Failed);
        CHECK(f.backend.pairingActive()); CHECK(f.backend.endPairing());
    }
    for (bool inWrite : {true, false}) {
        Fixture f; f.fake->throwNextWrite = inWrite; if (!inWrite) f.fake->reply = Reply::Throw;
        expectTransportException([&] { (void)f.backend.beginPairing(1); });
        CHECK(f.backend.pairingActive());
        CHECK(f.backend.pairingEnableExchange().write == (inWrite ? FeatureWriteState::OutcomeUnknown : FeatureWriteState::AcceptedByTransportApi));
        CHECK(f.backend.pairingEnableExchange().error == (inWrite ? FeatureExchangeError::WriteException : FeatureExchangeError::ResponseException));
        CHECK(f.backend.lastError().find("PRIVATE") == std::string::npos);
        f.fake->reply = Reply::Match; f.backend.stop(); CHECK(f.fake->trace->writes.size() == 2);
        pairingPacket(f.fake->trace->writes.back(), false, 1);
    }
    for (bool inWrite : {true, false}) {
        Fixture f; CHECK(f.backend.beginPairing(2));
        f.fake->throwNextWrite = inWrite; if (!inWrite) f.fake->reply = Reply::Throw;
        expectTransportException([&] { (void)f.backend.endPairing(); });
        CHECK(f.backend.pairingActive() && f.backend.pairingCleanupEvidence().state == CleanupExchangeState::Unknown);
        CHECK(f.backend.pairingCleanupEvidence().disable.write == (inWrite ? FeatureWriteState::OutcomeUnknown : FeatureWriteState::AcceptedByTransportApi));
        f.fake->reply = Reply::Match; CHECK(f.backend.endPairing());
    }
}
void operations() {
    {
        Fixture f; PairingController c(f.backend); f.fake->rejectNextWrite = true;
        CHECK(!c.begin(100, 2));
        CHECK(c.result().state == PairingState::Failed && c.result().cleanupConfirmed);
        CHECK(c.result().enableExchange.write == FeatureWriteState::Failed);
        CHECK(c.result().cleanup.state == CleanupExchangeState::NotNeeded);
        CHECK(c.result().associationEffect == SemanticEffect::Unverified && f.fake->trace->writes.size() == 1);
    }
    for (const auto mode : {Reply::Empty, Reply::Short, Reply::Wrong, Reply::LowDeclaration}) {
        Fixture f; PairingController c(f.backend); f.fake->reply = mode;
        CHECK(!c.begin(100, 2));
        CHECK(c.result().state == PairingState::CleanupFailed && !c.result().cleanupConfirmed);
        CHECK(c.result().enableExchange.write == FeatureWriteState::AcceptedByTransportApi);
        CHECK(c.result().cleanup.state == CleanupExchangeState::Failed && f.backend.pairingActive());
        CHECK(c.result().cleanup.effect == SemanticEffect::Unverified && c.result().associationEffect == SemanticEffect::Unverified);
        f.fake->reply = Reply::Match; CHECK(c.cancel());
    }
    for (int finish = 0; finish < 4; ++finish) {
        Fixture f; PairingController controller(f.backend);
        CHECK(controller.begin(100, 2, std::chrono::seconds(1)));
        CHECK(controller.result().state == PairingState::Pairing && !controller.result().paired);
        CHECK(controller.result().enableExchange.matched());
        CHECK(controller.result().associationEffect == SemanticEffect::Unverified);
        if (finish == 0) CHECK(controller.cancel());
        if (finish == 1) controller.tick(1000000100);
        if (finish == 2) CHECK(controller.shutdown());
        if (finish == 3) {
            auto event = BackendEvent{BackendEvent::Kind::DevicePaired, pairing_test::device(2), {}, {}, 101, f.backend.transportGeneration()};
            for (auto bad : {BackendEvent{BackendEvent::Kind::DevicePaired, pairing_test::device(1), {}, {}, 101, f.backend.transportGeneration()},
                             BackendEvent{BackendEvent::Kind::DevicePaired, pairing_test::device(2), {}, {}, 99, f.backend.transportGeneration()},
                             BackendEvent{BackendEvent::Kind::DevicePaired, pairing_test::device(2), {}, {}, 101, f.backend.transportGeneration()+1},
                             BackendEvent{BackendEvent::Kind::DevicePaired, pairing_test::device(2), {}, {}, 1000000100, f.backend.transportGeneration()}}) {
                controller.observe(bad); CHECK(!controller.result().paired);
            }
            controller.observe(event); controller.tick(102);
            CHECK(controller.result().state == PairingState::Paired);
        }
        CHECK(controller.result().associationEffect == (finish == 3 ? SemanticEffect::ConfirmedByIndependentEvidence : SemanticEffect::Unverified));
        CHECK(controller.result().cleanup.state == CleanupExchangeState::Matched && controller.result().cleanupConfirmed);
        CHECK(controller.result().cleanup.effect == SemanticEffect::Unverified);
        CHECK(f.fake->trace->writes.size() == 2); pairingPacket(f.fake->trace->writes[0], true, 2); pairingPacket(f.fake->trace->writes[1], false, 2);
        CHECK(controller.cancel() && controller.shutdown() && controller.shutdown());
        f.backend.stop(); CHECK(f.fake->trace->writes.size() == 2);
    }
    {
        Fixture f; PairingController controller(f.backend); f.fake->throwNextWrite = true;
        expectTransportException([&] { (void)controller.begin(100, 1); });
        CHECK(controller.result().enableExchange.write == FeatureWriteState::OutcomeUnknown);
        CHECK(controller.result().state == PairingState::Failed && controller.result().cleanupConfirmed);
        CHECK(controller.result().cleanup.effect == SemanticEffect::Unverified);
    }
    for (int action = 0; action < 3; ++action) {
        Fixture f; PairingController c(f.backend); CHECK(c.begin(100, 2, std::chrono::seconds(1)));
        f.fake->reply = Reply::Empty;
        if (action == 0) CHECK(!c.cancel());
        if (action == 1) c.tick(1000000100);
        if (action == 2) CHECK(!c.shutdown());
        CHECK(c.result().state == PairingState::CleanupFailed && !c.result().cleanupConfirmed);
        CHECK(c.result().cleanup.effect == SemanticEffect::Unverified && c.result().associationEffect == SemanticEffect::Unverified);
        CHECK(f.fake->trace->writes.size() == 3);
        f.fake->reply = Reply::Match;
        if (action == 2) {
            CHECK(!c.shutdown() && f.fake->trace->writes.size() == 3); // failed shutdown stays truthful and idempotent
            f.backend.stop(); CHECK(f.fake->trace->writes.size() == 4);
        } else CHECK(c.cancel());
    }
    {
        Fixture f; PairingController c(f.backend); f.fake->reply = Reply::Throw;
        expectTransportException([&] { (void)c.begin(100, 0); });
        CHECK(c.result().enableExchange.write == FeatureWriteState::AcceptedByTransportApi);
        CHECK(c.result().state == PairingState::CleanupFailed && c.result().cleanup.state == CleanupExchangeState::Unknown);
        CHECK(f.backend.pairingActive() && c.result().cleanup.effect == SemanticEffect::Unverified);
        f.fake->reply = Reply::Match; CHECK(c.cancel());
    }
    {
        Fixture f;
        { PairingController controller(f.backend); CHECK(controller.begin(100)); }
        CHECK(f.fake->trace->writes.size() == 2 && !f.backend.pairingActive());
        f.backend.stop(); CHECK(f.fake->trace->writes.size() == 2);
    }
    {
        auto transport = std::make_unique<Transport>(); auto trace = transport->trace;
        { Backend backend(std::move(transport)); CHECK(backend.start()); CHECK(backend.beginPairing(0)); }
        CHECK(trace->writes.size() == 2); pairingPacket(trace->writes.back(), false, 0);
    }
}
void lossGenerationAndStop() {
    {
        Fixture f; PairingController c(f.backend); CHECK(c.begin(100, 1)); f.fake->lost = true;
        CHECK(!f.backend.pollOnce()); c.transportLost(f.backend.lastError());
        CHECK(c.result().cleanupConfirmed && c.result().cleanup.state == CleanupExchangeState::Matched);
        CHECK(c.result().cleanup.effect == SemanticEffect::Unverified);
        CHECK(c.result().associationEffect == SemanticEffect::Unverified);
    }
    {
        Fixture f; PairingController c(f.backend); CHECK(c.begin(100, 1)); f.fake->reply = Reply::Empty;
        CHECK(!c.cancel()); CHECK(c.result().state == PairingState::CleanupFailed);
        f.backend.stop();
        CHECK(f.backend.pairingActive() && f.backend.pairingCleanupEvidence().state == CleanupExchangeState::Failed);
        const auto before = f.fake->trace->writes.size(); f.fake->reply = Reply::Match;
        BackendConfig config; config.configureTrackingOnPair = false;
        CHECK(f.backend.start(config)); CHECK(f.fake->trace->writes.size() == before+1);
        CHECK(f.backend.pairingCleanupEvidence().disable.matched());
        c.tick(101);
        CHECK(c.result().state == PairingState::CleanupFailed && !c.result().cleanupConfirmed);
        CHECK(c.result().cleanup.state == CleanupExchangeState::Unknown);
        CHECK(c.result().cleanup.effect == SemanticEffect::Unverified);
        CHECK(!c.shutdown() && f.fake->trace->writes.size() == before+1);
    }
    {
        Fixture f; PairingController c(f.backend); CHECK(c.begin(100, 3));
        f.fake->reply = Reply::Empty; f.fake->lost = true;
        CHECK(!f.backend.pollOnce()); c.transportLost(f.backend.lastError());
        CHECK(c.result().state == PairingState::CleanupFailed && !c.result().cleanupConfirmed);
        CHECK(c.result().cleanup.disable.response == FeatureResponseState::NotObserved);
        CHECK(f.fake->trace->writes.size() == 4); // original two loss retries + final stop retry
        f.fake->reply = Reply::Match;
    }
    {
        Fixture f; CHECK(f.backend.beginPairing(0)); f.fake->reply = Reply::Empty; f.fake->throwClose = true;
        f.backend.stop();
        CHECK(!f.backend.running() && f.backend.pairingActive());
        CHECK(f.backend.pairingCleanupEvidence().state == CleanupExchangeState::Failed);
        CHECK(f.backend.pairingCleanupEvidence().disable.response == FeatureResponseState::NotObserved);
        CHECK(f.backend.lastError().find("PRIVATE") == std::string::npos);
        const auto writes = f.fake->trace->writes.size(); f.backend.stop(); CHECK(f.fake->trace->writes.size() == writes);
        f.fake->throwClose = false;
    }
}
std::vector<std::uint8_t> ackPacket(int slot, const std::string& text) {
    // Literal baseline wire framing; intentionally independent of production builders.
    std::vector<std::uint8_t> p(65, 0); p[1]=0x18; p[2]=static_cast<std::uint8_t>(12+text.size()); p[3]=corruptWireFixture ? 3 : 4;
    const auto d = pairing_test::device(slot); std::copy(d.mac.begin(),d.mac.end(),p.begin()+4);
    p[10]=0; p[11]=1; p[12]=static_cast<std::uint8_t>(text.size()); std::copy(text.begin(),text.end(),p.begin()+13);
    return p;
}
std::vector<std::uint8_t> infoReport(int slot) {
    const auto d = pairing_test::device(slot); std::vector<std::uint8_t> p(16,0);
    p[0]=0x28; std::copy(d.mac.begin(),d.mac.end(),p.begin()+3); p[9]=1; p[10]=1; p[11]=4;
    std::copy_n("NAZZ",4,p.begin()+12); return p;
}
void unchangedWireAndAckFailure() {
    Fixture f; BackendConfig config; CHECK(f.backend.start(config));
    // First configure send is rejected; later sends still run in original order,
    // while the failure remains available instead of being silently discarded.
    f.fake->rejectNextWrite = true;
    f.fake->reads.push_back(pairing_test::pairReport(2)); CHECK(f.backend.pollOnce());
    std::vector<std::vector<std::uint8_t>> expected;
    for (const auto text : {"ARI1","ATM-1","WcJP","ATH1","AWH1","ANI0","ATM20"}) expected.push_back(ackPacket(2,text));
    CHECK(f.fake->trace->writes == expected);
    CHECK(f.backend.lastFailedFeatureExchange() && f.backend.lastFailedFeatureExchange()->error == FeatureExchangeError::WriteRejected);
    CHECK(f.backend.lastFeatureExchange().matched());
    CHECK(f.backend.lastError().find("feature write rejected") != std::string::npos);
    f.fake->reads.push_back(pairing_test::pairReport(1)); CHECK(f.backend.pollOnce());
    for (const auto text : {"ARI1","ATM-1","ATH0","AWH0","ANI1","ATM11"}) expected.push_back(ackPacket(1,text));
    CHECK(f.fake->trace->writes == expected);
    f.fake->reply = Reply::Wrong; f.fake->reads.push_back(infoReport(1)); CHECK(f.backend.pollOnce());
    expected.push_back(ackPacket(1,"FW1")); CHECK(f.fake->trace->writes == expected);
    CHECK(f.backend.lastFailedFeatureExchange()->response == FeatureResponseState::WrongCommandOnly);
    CHECK(f.backend.lastFeatureExchange().responseAttempts == 10);
    f.fake->reply = Reply::Match;
    auto unpair = pairing_test::pairReport(1); unpair[5]=1; f.fake->reads.push_back(unpair); CHECK(f.backend.pollOnce());
    auto pws=std::vector<std::uint8_t>(65,0);pws[1]=0x1d;pws[2]=9;pws[3]=1;pws[4]=0;pws[6]=1;
    expected.push_back(pws); CHECK(f.fake->trace->writes == expected);
    CHECK(f.backend.beginPairing(4)); pairingPacket(f.fake->trace->writes.back(),true,4);
    CHECK(f.backend.endPairing()); pairingPacket(f.fake->trace->writes.back(),false,4);
}
}
int main(int argc, char** argv) try {
    corruptWireFixture = argc == 2 && std::string(argv[1]) == "--corrupt-wire-fixture";
    writeAndResponses(); disableMatrixAndExceptions(); operations(); lossGenerationAndStop(); unchangedWireAndAckFailure();
    std::cout << "PASS typed feature exchange, independent association, cleanup uncertainty, generation, exceptions/destructors and unchanged baseline wire; hardware NOT RUN\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
