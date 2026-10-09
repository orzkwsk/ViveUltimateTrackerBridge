#include "vut_epoch_probe_support.hpp"
#include "fake_pairing_transport.hpp"

#include <deque>
#include <iostream>
#include <sstream>
#include <stdexcept>

#define CHECK(x) do { if (!(x)) throw std::runtime_error("CHECK failed: " #x); } while (false)

namespace {
using namespace vut;
using namespace vut::epoch_probe;
// Synthetic fixtures only. These lengths match the Stage 1 gate, not identity.
const std::array<std::string_view, 9> burst{
    "NANAABC", "NADSABCDEFGHIJKL", "NASSABCDEFGHIJKLM", "NASI12345678",
    "NAPIABCDEFGH", "NAV1ABCDEFGHIJK", "NARI1", "NAGNABCDE", "NAZZ"};
void redactionAndFormatting(bool emit) {
    std::ostringstream output, copy;
    Receipt receipt(output, &copy); receipt.time(12); receipt.mark(Mark::Started, 8);
    BackendEvent event; event.transportGeneration = 8; event.device = pairing_test::device();
    event.kind = BackendEvent::Kind::DevicePaired; event.text = "synthetic-sensitive-diagnostic";
    receipt.event(event); event.kind = BackendEvent::Kind::Ack;
    for (auto text : burst) { event.text = text; receipt.event(event); }
    for (auto text : {"NXYZsynthetic-secret", "ordinary-synthetic-secret", "", "NAZZgarbage", "NADS\n\"secret"}) {
        event.text = text; receipt.event(event);
    }
    event.kind = BackendEvent::Kind::Diagnostic; receipt.event(event);
    event.kind = BackendEvent::Kind::Pose; receipt.event(event);
    ConnectionEpochSnapshot snapshot; snapshot.connectionEpochId = 19; snapshot.transportGeneration = 8;
    snapshot.slot = 2; snapshot.phase = ConnectionPhase::InfoComplete; snapshot.expectedIndex = 9;
    snapshot.seenMask = 511; snapshot.active = snapshot.infoComplete = snapshot.initializationEligible = true;
    receipt.epoch(snapshot); receipt.result(Result::FreshBurst, 8);
    CHECK(output.str() == copy.str());
    for (auto secret : {"ABCDEFGHIJKL", "ABCDEFGHIJKLM", "synthetic-secret", "synthetic-sensitive", "garbage",
                       "23:32:03:81:fe:ec", "secret", "12345678"}) CHECK(output.str().find(secret) == std::string::npos);
    CHECK(output.str().find("\"epoch\":19") != std::string::npos);
    CHECK(output.str().find("\"progress\":9") != std::string::npos);
    CHECK(output.str().find("\"initialization_eligible\":true") != std::string::npos);
    CHECK(ackLabel("NADS") == "NADS" && ackLabel("N") == "ACK_UNKNOWN_N");
    for (int i = 0; i <= static_cast<int>(ConnectionEpochReason::ProgressExhausted); ++i)
        CHECK(reasonName(static_cast<ConnectionEpochReason>(i)) != "Unknown");
    if (emit) std::cout << output.str();
}

#ifdef _WIN32
class Transport final : public detail::BackendTransport {
public:
    bool exclusive = false, closed = true, failOpen = false, failRead = false, throwRead = false;
    unsigned writes = 0, gets = 0;
    std::deque<std::vector<std::uint8_t>> reads;
    bool open(std::uint16_t vid, std::uint16_t pid, bool value, std::string& error) override {
        CHECK(vid == 0x0bb4 && pid == 0x0350); exclusive = value;
        if (failOpen) { error = "synthetic-private-identity-in-error"; return false; }
        closed = false; return true;
    }
    void close() override { closed = true; }
    std::size_t featureLength() const noexcept override { return 65; }
    bool setFeature(std::span<const std::uint8_t>) override { ++writes; return true; }
    std::vector<std::uint8_t> getFeature() override { ++gets; return {}; }
    std::vector<std::uint8_t> read(int) override {
        if (throwRead) throw std::runtime_error("synthetic-private-read-error");
        if (reads.empty()) return {};
        auto packet = std::move(reads.front()); reads.pop_front(); return packet;
    }
    bool readFailed() const noexcept override { return failRead; }
};
std::vector<std::uint8_t> incoming(std::string_view text, bool pose = false) {
    const auto device = pairing_test::device();
    const std::size_t size = pose ? 37 : text.size();
    std::vector<std::uint8_t> report(12 + size, 0); report[0] = 0x28;
    std::copy(device.mac.begin(), device.mac.end(), report.begin() + 3);
    report[9] = pose ? 0x10 : 0x01; report[10] = 0x01; report[11] = static_cast<std::uint8_t>(size);
    if (pose) { report[12+15] = 0x3c; report[12+36] = 3; }
    else std::copy(text.begin(), text.end(), report.begin() + 12);
    return report;
}
struct Fixture {
    Transport* fake;
    Backend backend;
    std::ostringstream output, copy;
    Receipt receipt;
    Session session;
    Fixture() : Fixture(std::make_unique<Transport>()) {}
    explicit Fixture(std::unique_ptr<Transport> t) : fake(t.get()), backend(std::move(t)),
        receipt(output, &copy), session(backend, receipt) {}
    void feed(std::vector<std::uint8_t> packet) { fake->reads.push_back(std::move(packet)); session.poll(); }
    void arm() {
        CHECK(session.start() && fake->exclusive);
        CHECK(!session.request(4999)); CHECK(session.request(5000)); CHECK(!session.request(5001));
        CHECK(session.confirm(6000)); CHECK(!session.confirm(6001));
    }
    void fullBurst() { for (auto text : burst) feed(incoming(text)); }
    void stopCheck() {
        session.stop(); CHECK(fake->closed && fake->writes == 0 && fake->gets == 0);
        CHECK(output.str() == copy.str());
        CHECK(output.str().find("private") == std::string::npos);
    }
};
void fakeSessionMatrix() {
    const auto config = fixedConfig();
    CHECK(config.automaticVendorActionPolicy == AutomaticVendorActionPolicy::ObserveOnly);
    CHECK(config.exclusiveHidAccess && !config.configureTrackingOnPair && !config.diagnosticSlamQueries);
    for (bool pose : {false, true}) {
        Fixture f; f.arm(); f.feed(pairing_test::pairReport()); f.fullBurst();
        if (pose) f.feed(incoming({}, true));
        const auto before = f.output.str();
        for (int i = 0; i < 100; ++i) f.session.poll();
        if (pose) for (int i = 0; i < 100; ++i) f.feed(incoming({}, true));
        CHECK(f.output.str() == before); // no unchanged epochs or per-pose flood
        f.session.tick(95999, 90); CHECK(f.session.result() == Result::Pending);
        f.session.tick(96000, 90); CHECK(f.session.result() == Result::FreshBurst); f.stopCheck();
    }
    {
        Fixture f; f.arm(); f.session.tick(96000, 90);
        CHECK(f.session.result() == Result::NoAssociation); f.stopCheck();
    }
    {
        Fixture f; f.arm(); f.feed(pairing_test::pairReport()); f.feed(incoming(burst[0]));
        f.session.tick(96000, 90); CHECK(f.session.result() == Result::IncompleteInfo); f.stopCheck();
    }
    for (auto bad : {"NANAABC", "NASSABCDEFGHIJKLM", "NADSshort", "NXYZprivate"}) {
        Fixture f; f.arm(); f.feed(pairing_test::pairReport()); f.feed(incoming(burst[0])); f.feed(incoming(bad));
        CHECK(f.session.result() == Result::Violation);
        const auto before = f.output.str(); f.fullBurst(); f.feed(pairing_test::pairReport());
        CHECK(f.output.str() == before && f.session.result() == Result::Violation); f.stopCheck();
    }
    {
        Fixture f; f.arm(); f.feed(pairing_test::pairReport()); f.feed(incoming({}, true));
        CHECK(f.session.result() == Result::Violation);
        CHECK(f.output.str().find("PoseBeforeInfo") != std::string::npos); f.stopCheck();
    }
    {
        Fixture f; CHECK(f.session.start()); f.feed(pairing_test::pairReport());
        CHECK(f.session.result() == Result::Ambiguous); f.stopCheck();
    }
    {
        Fixture f; f.arm(); f.feed(pairing_test::pairReport());
        f.feed(pairing_test::pairReport(1)); CHECK(f.session.result() == Result::Ambiguous); f.stopCheck();
    }
    {
        auto fake = std::make_unique<Transport>(); fake->failOpen = true; Fixture f(std::move(fake));
        CHECK(!f.session.start() && f.fake->exclusive); CHECK(f.session.result() == Result::Ambiguous); f.stopCheck();
    }
    {
        Fixture f; f.arm(); f.fake->failRead = true; f.session.poll();
        CHECK(f.session.result() == Result::Ambiguous); f.stopCheck();
    }
    {
        Fixture f; f.arm(); f.session.abort(); CHECK(f.session.result() == Result::Ambiguous); f.stopCheck();
    }
    {
        Fixture f; CHECK(f.session.start()); CHECK(!f.session.confirm(5000));
        f.session.tick(900000, 90); CHECK(f.session.result() == Result::Ambiguous); f.stopCheck();
    }
    {
        Fixture f; f.arm(); f.feed(pairing_test::pairReport()); f.fullBurst();
        auto unpair = pairing_test::pairReport(); unpair[5] = 1; f.feed(unpair);
        f.session.tick(96000, 90); CHECK(f.session.result() == Result::IncompleteInfo); f.stopCheck();
    }
}
#endif
}

int main(int argc, char** argv) try {
    const bool emit = argc == 2 && std::string_view(argv[1]) == "--emit-fixture";
    redactionAndFormatting(emit);
#ifdef _WIN32
    fakeSessionMatrix();
#endif
    if (!emit) std::cout << "PASS epoch probe fixed policy, sanitized receipt, bounded passive classification; hardware NOT RUN\n";
    return 0;
} catch (...) { std::cerr << "epoch probe offline test failed\n"; return 1; }
