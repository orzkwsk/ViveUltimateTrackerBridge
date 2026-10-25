#include "vut/backend.hpp"
#include "vut/backend_transport.hpp"
#include "vut/detail/info_epoch.hpp"
#include "fake_pairing_transport.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <thread>

#define CHECK(x) do { if (!(x)) throw std::runtime_error("CHECK failed: " #x); } while (false)

namespace {
using namespace vut;
using detail::ConnectionEpochTracker;
// Synthetic payloads only; lengths/character classes match the reviewed
// current official profile. No captured serial, RF or identity candidate.
const std::array<std::string_view, 9> burst{
    "NANAABC", "NADSABCDEFGHIJKL", "NASSABCDEFGHIJKLM", "NASI12345678",
    "NAPIABCDEFGH", "NAV1ABCDEFGHIJK", "NARI1", "NAGNABCDE", "NAZZ"
};
bool corruptEpochFixture = false;
auto fields(const ConnectionEpochSnapshot& s) {
    return std::tuple{s.connectionEpochId, s.transportGeneration, s.slot, s.phase, s.infoState,
        s.expectedIndex, s.seenMask, s.active, s.infoComplete, s.initializationEligible, s.poseObserved, s.reason};
}
ConnectionEpochSnapshot current(const ConnectionEpochTracker& tracker, int slot = 2) {
    const auto all = tracker.snapshot();
    const auto found = std::find_if(all.rbegin(), all.rend(), [slot](const auto& s) { return s.slot == slot; });
    CHECK(found != all.rend()); return *found;
}
void fullBurst(ConnectionEpochTracker& tracker, std::uint64_t generation, const DeviceId& d) {
    for (std::size_t i = 0; i < burst.size(); ++i)
        tracker.ack(generation, d, corruptEpochFixture && i == 2 ? burst[1] : burst[i]);
}
void closed(const ConnectionEpochSnapshot& s, ConnectionEpochReason reason) {
    CHECK(!s.active && !s.infoComplete && !s.initializationEligible);
    CHECK(s.infoState == InfoCollectionState::Invalid && s.reason == reason);
    CHECK(!s.nadsFresh && !s.nassFresh && !s.deviceCandidate);
}
void happyAndFreshEpochs() {
    ConnectionEpochTracker tracker; tracker.transportOpened(30);
    const auto d = pairing_test::device();
    CHECK(tracker.paired(30, d));
    const auto first = current(tracker);
    CHECK(first.connectionEpochId == 1 && first.transportGeneration == 30);
    CHECK(first.phase == ConnectionPhase::InfoCollecting && first.active && first.expectedIndex == 0);
    for (std::size_t i = 0; i < burst.size(); ++i) {
        tracker.ack(30, d, burst[i]); const auto s = current(tracker);
        CHECK(s.expectedIndex == i+1 && s.seenMask == (1u << (i+1))-1u);
        CHECK(s.infoComplete == (i == 8) && s.initializationEligible == (i == 8));
        CHECK(!s.poseObserved);
        CHECK(s.nadsPresent == (i >= 1) && s.nadsFresh == (i >= 1));
        CHECK(s.nassPresent == (i >= 2) && s.nassFresh == (i >= 2));
        CHECK(s.deviceCandidate && s.deviceCandidate->mac == d.mac && s.deviceCandidate->slot == d.slot);
    }
    CHECK(current(tracker).phase == ConnectionPhase::InfoComplete);
    tracker.pose(30, d);
    CHECK(current(tracker).poseObserved && current(tracker).initializationEligible);
    CHECK(current(tracker).phase == ConnectionPhase::PoseObserved);
    tracker.unpaired(30, d);
    const auto disconnected = current(tracker);
    closed(disconnected, ConnectionEpochReason::DeviceUnpaired);
    CHECK(disconnected.phase == ConnectionPhase::Disconnected && disconnected.poseObserved);
    for (auto text : burst) tracker.ack(30, d, text);
    tracker.pose(30, d); tracker.unpaired(30, d);
    CHECK(fields(current(tracker)) == fields(disconnected));
    CHECK(tracker.paired(30, d));
    const auto second = current(tracker);
    CHECK(second.connectionEpochId == 2 && second.transportGeneration == 30);
    CHECK(!second.infoComplete && !second.poseObserved && second.seenMask == 0 && second.expectedIndex == 0);
    fullBurst(tracker, 30, d); CHECK(current(tracker).infoComplete);
    // Same-destination DevicePaired is a new association input, not idempotent.
    CHECK(tracker.paired(30, d));
    CHECK(current(tracker).connectionEpochId == 3 && current(tracker).expectedIndex == 0);
    CHECK(tracker.snapshot().size() == 2);
    closed(tracker.snapshot().front(), ConnectionEpochReason::Superseded);
}
void exactOrderFailures() {
    const auto d = pairing_test::device();
    for (std::size_t omitted = 0; omitted < burst.size(); ++omitted) {
        ConnectionEpochTracker t; t.transportOpened(1); CHECK(t.paired(1, d));
        for (std::size_t i = 0; i < burst.size(); ++i) if (i != omitted) t.ack(1, d, burst[i]);
        CHECK(!current(t).infoComplete && !current(t).initializationEligible);
        if (omitted != 8) closed(current(t), ConnectionEpochReason::OutOfOrderOrMissingToken);
        else {
            // No arbitrary timer: a missing final token stays incomplete until
            // a closing event; it never acquires initialization eligibility.
            CHECK(current(t).active && current(t).expectedIndex == 8);
            t.unpaired(1, d); closed(current(t), ConnectionEpochReason::DeviceUnpaired);
        }
    }
    for (std::size_t swapped = 0; swapped+1 < burst.size(); ++swapped) {
        ConnectionEpochTracker t; t.transportOpened(1); CHECK(t.paired(1, d));
        for (std::size_t i = 0; i < swapped; ++i) t.ack(1, d, burst[i]);
        t.ack(1, d, burst[swapped+1]); t.ack(1, d, burst[swapped]);
        closed(current(t), ConnectionEpochReason::OutOfOrderOrMissingToken);
        const auto failed = current(t); fullBurst(t, 1, d);
        CHECK(fields(current(t)) == fields(failed));
    }
    for (std::size_t duplicate = 0; duplicate < burst.size(); ++duplicate) {
        ConnectionEpochTracker t; t.transportOpened(1); CHECK(t.paired(1, d));
        for (std::size_t i = 0; i <= duplicate; ++i) t.ack(1, d, burst[i]);
        t.ack(1, d, burst[duplicate]); closed(current(t), ConnectionEpochReason::DuplicateToken);
        fullBurst(t, 1, d); CHECK(!current(t).infoComplete);
    }
}
void destinationAndGeneration() {
    const auto d = pairing_test::device();
    for (std::size_t byte = 0; byte < d.mac.size(); ++byte) {
        auto other = d; other.mac[byte] ^= 0x80; // low slot nibble preserved
        ConnectionEpochTracker t; t.transportOpened(7); CHECK(t.paired(7, d));
        t.ack(7, d, burst[0]); t.ack(7, other, burst[1]);
        closed(current(t), ConnectionEpochReason::DestinationConflict);
        CHECK(current(t).expectedIndex == 1);
        fullBurst(t, 7, d); CHECK(!current(t).infoComplete);
    }
    auto other = d; other.mac[5] ^= 1;
    ConnectionEpochTracker t; t.transportOpened(7); CHECK(t.paired(7, d)); fullBurst(t, 7, d);
    CHECK(t.paired(7, other));
    const auto previous = t.snapshot().front(); closed(previous, ConnectionEpochReason::Superseded);
    CHECK(current(t).connectionEpochId == 2 && current(t).seenMask == 0);
    t.ack(7, d, burst[0]); // late old destination conflicts with new active epoch
    CHECK(fields(t.snapshot().front()) == fields(previous));
    closed(current(t), ConnectionEpochReason::DestinationConflict);
    CHECK(t.paired(7, other)); fullBurst(t, 7, other); CHECK(current(t).infoComplete);
    t.transportOpened(8); closed(current(t), ConnectionEpochReason::TransportGenerationChanged);
    const auto retired = current(t);
    fullBurst(t, 7, other); t.pose(7, other); CHECK(!t.paired(7, other));
    CHECK(fields(current(t)) == fields(retired));
    fullBurst(t, 8, other); CHECK(fields(current(t)) == fields(retired)); // no pair in new generation
    CHECK(t.paired(8, other)); CHECK(current(t).connectionEpochId == 4 && current(t).seenMask == 0);
    t.ack(7, other, burst[0]); CHECK(current(t).expectedIndex == 0); // stale generation cannot mutate
    fullBurst(t, 8, other); CHECK(current(t).infoComplete);
    t.unpaired(8, d); CHECK(current(t).infoComplete); // late old unpair ignored
    t.unpaired(8, other); closed(current(t), ConnectionEpochReason::DeviceUnpaired);

    // Independent slots, no implicit epoch from ACK/pose before association.
    ConnectionEpochTracker multiple; multiple.transportOpened(3);
    multiple.ack(3, d, burst[0]); multiple.pose(3, d); CHECK(multiple.snapshot().empty());
    for (int slot = 0; slot < 5; ++slot) CHECK(multiple.paired(3, pairing_test::device(slot)));
    fullBurst(multiple, 3, d); CHECK(current(multiple).infoComplete);
    CHECK(current(multiple, 1).expectedIndex == 0);
    auto invalid = d; invalid.slot = -1; CHECK(!multiple.paired(3, invalid));
    invalid.slot = 5; CHECK(!multiple.paired(3, invalid));
    for (int i = 0; i < 1000; ++i) CHECK(multiple.paired(3, d));
    CHECK(multiple.snapshot().size() == 6); // at most two records per slot
    multiple.invalidateAll(ConnectionEpochReason::BackendStopped);
    for (const auto& s : multiple.snapshot()) CHECK(!s.active && !s.initializationEligible);
}
void shapesPoseAndUnrelated() {
    const auto d = pairing_test::device();
    for (std::size_t count = 0; count < burst.size(); ++count) {
        ConnectionEpochTracker t; t.transportOpened(1); CHECK(t.paired(1, d));
        for (std::size_t i = 0; i < count; ++i) t.ack(1, d, burst[i]);
        t.pose(1, d); CHECK(current(t).poseObserved);
        closed(current(t), ConnectionEpochReason::PoseBeforeInfo);
        fullBurst(t, 1, d); CHECK(!current(t).infoComplete && !current(t).initializationEligible);
    }
    for (std::size_t token = 0; token < burst.size(); ++token) {
        for (int malformed = 0; malformed < 3; ++malformed) {
            ConnectionEpochTracker t; t.transportOpened(1); CHECK(t.paired(1, d));
            for (std::size_t i = 0; i < token; ++i) t.ack(1, d, burst[i]);
            std::string bad(burst[token]);
            if (malformed == 0) bad += 'x';
            if (malformed == 1) { if (token == 8) bad += "garbage"; else bad.pop_back(); }
            if (malformed == 2) { if (token == 8) bad += '\0'; else bad.back() = '\0'; }
            t.ack(1, d, bad); closed(current(t), ConnectionEpochReason::MalformedToken);
        }
    }
    for (auto bad : {"NASIabcdefgh", "NARIx"}) {
        ConnectionEpochTracker t; t.transportOpened(1); CHECK(t.paired(1, d));
        t.ack(1, d, bad); closed(current(t), ConnectionEpochReason::MalformedToken);
    }
    for (std::size_t count = 0; count <= burst.size(); ++count) {
        ConnectionEpochTracker t; t.transportOpened(1); CHECK(t.paired(1, d));
        for (std::size_t i = 0; i < count; ++i) t.ack(1, d, burst[i]);
        const auto before = current(t);
        for (auto text : {"ARI1", "FW1", "", "ordinary"}) t.ack(1, d, text);
        CHECK(fields(current(t)) == fields(before));
        t.ack(1, d, "NXYZunknown"); closed(current(t), ConnectionEpochReason::UnknownInfoToken);
    }
}
void exhaustion() {
    const auto d = pairing_test::device();
    const auto max = std::numeric_limits<std::uint64_t>::max();
    ConnectionEpochTracker t(max-1); t.transportOpened(1); CHECK(t.paired(1, d));
    CHECK(current(t).connectionEpochId == max);
    CHECK(!t.paired(1, d) && t.exhausted()); closed(current(t), ConnectionEpochReason::EpochCounterExhausted);
    t.transportOpened(2); CHECK(!t.paired(2, d)); CHECK(current(t).connectionEpochId == max);
    ConnectionEpochTracker empty(max); empty.transportOpened(1);
    CHECK(!empty.paired(1, d) && empty.exhausted() && empty.snapshot().empty());
    for (auto index : {10, 255}) {
        detail::InfoBurstProgress p; p.expectedIndex = static_cast<std::uint8_t>(index); p.seenMask = 0xffff;
        CHECK(detail::advanceInfoBurst(p, "NAZZ") == ConnectionEpochReason::ProgressExhausted);
        CHECK(p.state == InfoCollectionState::Invalid && p.expectedIndex == index);
    }
    detail::InfoBurstProgress p; p.seenMask = 0xffff;
    CHECK(detail::advanceInfoBurst(p, burst[0]) == ConnectionEpochReason::ProgressExhausted);
    p = {}; p.state = InfoCollectionState::Complete;
    CHECK(detail::advanceInfoBurst(p, "ordinary") == ConnectionEpochReason::ProgressExhausted);
}

#ifdef _WIN32
class Transport final : public detail::BackendTransport {
public:
    std::vector<std::vector<std::uint8_t>> writes;
    std::deque<std::vector<std::uint8_t>> reads;
    std::function<void()> beforeWrite;
    bool lost = false, throwRead = false;
    std::uint8_t command = 0;
    bool open(std::uint16_t, std::uint16_t, bool, std::string&) override { lost = throwRead = false; return true; }
    void close() override {}
    std::size_t featureLength() const noexcept override { return 65; }
    bool setFeature(std::span<const std::uint8_t> p) override {
        if (beforeWrite) beforeWrite();
        writes.emplace_back(p.begin(), p.end()); command = p[1]; return true;
    }
    std::vector<std::uint8_t> getFeature() override { return {0, command, 4, 0, 0}; }
    std::vector<std::uint8_t> read(int) override {
        if (throwRead) throw std::runtime_error("synthetic read exception");
        if (reads.empty()) return {};
        auto p = std::move(reads.front()); reads.pop_front(); return p;
    }
    bool readFailed() const noexcept override { return lost; }
};
std::vector<std::uint8_t> pair(const DeviceId& d, bool unpair = false) {
    auto p = pairing_test::pairReport(d.slot); p[5] = unpair;
    std::copy(d.mac.begin(), d.mac.end(), p.begin()+6); return p;
}
std::vector<std::uint8_t> incoming(const DeviceId& d, std::uint16_t type, std::span<const std::uint8_t> data) {
    std::vector<std::uint8_t> p(12+data.size(), 0); p[0] = 0x28;
    std::copy(d.mac.begin(), d.mac.end(), p.begin()+3);
    p[9] = static_cast<std::uint8_t>(type); p[10] = static_cast<std::uint8_t>(type >> 8);
    p[11] = static_cast<std::uint8_t>(data.size()); std::copy(data.begin(), data.end(), p.begin()+12); return p;
}
std::vector<std::uint8_t> ack(const DeviceId& d, std::string_view text) {
    return incoming(d, 0x0101, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}
std::vector<std::uint8_t> poseReport(const DeviceId& d, std::uint8_t status = 3) {
    std::vector<std::uint8_t> data(37, 0); data[15] = 0x3c; data[36] = status;
    return incoming(d, 0x0110, data);
}
struct Fixture {
    Transport* fake;
    Backend backend;
    explicit Fixture(const BackendConfig& config = {}) : Fixture(std::make_unique<Transport>(), config) {}
    Fixture(std::unique_ptr<Transport> t, const BackendConfig& config) : fake(t.get()), backend(std::move(t)) { CHECK(backend.start(config)); }
    void feed(std::vector<std::uint8_t> p) { fake->reads.push_back(std::move(p)); CHECK(backend.pollOnce()); }
    ConnectionEpochSnapshot epoch() const { const auto s = backend.connectionEpochSnapshot(); CHECK(!s.empty()); return s.back(); }
    void burstInput(const DeviceId& d) { for (auto text : burst) feed(ack(d, text)); }
};
void observeOnlyAndExplicitOperations() {
    const auto d = pairing_test::device();
    for (bool configure : {false, true}) for (bool diagnostic : {false, true}) {
        BackendConfig config; config.automaticVendorActionPolicy = AutomaticVendorActionPolicy::ObserveOnly;
        config.configureTrackingOnPair = configure; config.diagnosticSlamQueries = diagnostic;
        Fixture f(config); std::vector<BackendEvent::Kind> events; bool retired = false;
        f.backend.setEventCallback([&](const auto& e) {
            events.push_back(e.kind); CHECK(f.fake->writes.empty());
            if (e.kind == BackendEvent::Kind::DevicePaired) CHECK(f.epoch().phase == ConnectionPhase::InfoCollecting);
            if (e.kind == BackendEvent::Kind::Ack && e.text == "NAZZ") {
                if (!retired) CHECK(f.epoch().infoComplete && f.epoch().initializationEligible);
                else CHECK(!f.epoch().infoComplete && !f.epoch().initializationEligible);
            }
            if (e.kind == BackendEvent::Kind::Pose) {
                CHECK(f.epoch().poseObserved && f.epoch().initializationEligible);
                CHECK(e.pose && e.pose->trackingStatus == TrackingStatus::Lost);
            }
            if (e.kind == BackendEvent::Kind::DeviceUnpaired) {
                retired = true; closed(f.epoch(), ConnectionEpochReason::DeviceUnpaired);
            }
        });
        f.feed(pair(d)); f.burstInput(d); f.feed(poseReport(d)); f.feed(pair(d, true));
        CHECK(events.size() == 12 && f.fake->writes.empty());
        CHECK(f.backend.snapshot().empty()); // existing unpair/pose publication intact
        const auto closedEpoch = f.epoch(); f.feed(ack(d, "NAZZ"));
        CHECK(fields(f.epoch()) == fields(closedEpoch) && f.fake->writes.empty());
        f.backend.setEventCallback({});
        CHECK(f.backend.beginPairing(2)); CHECK(f.backend.endPairing());
        CHECK(f.fake->writes.size() == 2); // explicit operations remain usable
        const auto count = f.fake->writes.size(); f.backend.stop(); CHECK(f.fake->writes.size() == count);
    }
}
void observeOnlyOverridesDiagnosticQueries() {
    const auto d = pairing_test::device();
    BackendConfig config; config.diagnosticSlamQueries = true;
    Fixture legacy(config);
    config.automaticVendorActionPolicy = AutomaticVendorActionPolicy::ObserveOnly;
    Fixture passive(config);
    legacy.feed(pair(d)); legacy.burstInput(d); legacy.feed(poseReport(d));
    passive.feed(pair(d)); passive.burstInput(d); passive.feed(poseReport(d));
    CHECK(legacy.fake->writes.size() == 8 && passive.fake->writes.empty());
    // Exercise the real diagnostic due boundary using only in-memory reports.
    // No production clock/worker API is introduced for a test shortcut.
    std::this_thread::sleep_for(std::chrono::milliseconds(5100));
    legacy.feed(poseReport(d)); passive.feed(poseReport(d));
    CHECK(legacy.fake->writes.size() == 12 && passive.fake->writes.empty());
    CHECK(passive.epoch().poseObserved && passive.epoch().initializationEligible);
}
void backendFailureAndLifecycle() {
    BackendConfig config; config.automaticVendorActionPolicy = AutomaticVendorActionPolicy::ObserveOnly;
    const auto d = pairing_test::device();
    for (int loss = 0; loss < 3; ++loss) {
        Fixture f(config); f.feed(pair(d)); f.burstInput(d); CHECK(f.epoch().infoComplete);
        const auto first = f.epoch();
        if (loss == 0) f.backend.stop();
        if (loss == 1) { f.fake->lost = true; CHECK(!f.backend.pollOnce()); }
        if (loss == 2) {
            f.fake->throwRead = true; bool threw = false;
            try { (void)f.backend.pollOnce(); } catch (const std::runtime_error&) { threw = true; }
            CHECK(threw); f.fake->throwRead = false;
        }
        closed(f.epoch(), loss == 0 ? ConnectionEpochReason::BackendStopped : ConnectionEpochReason::TransportLost);
        CHECK(f.backend.start(config)); CHECK(f.backend.transportGeneration() > first.transportGeneration);
        f.feed(ack(d, "NAZZ")); CHECK(!f.epoch().infoComplete);
        f.feed(pair(d)); CHECK(f.epoch().connectionEpochId > first.connectionEpochId);
        CHECK(f.epoch().expectedIndex == 0 && !f.epoch().poseObserved);
        f.burstInput(d); CHECK(f.epoch().infoComplete && f.fake->writes.empty());
    }
    Fixture f(config); f.feed(pair(d)); f.feed(poseReport(d, 2)); // even Ok arrival before info is anomalous
    CHECK(f.epoch().poseObserved); closed(f.epoch(), ConnectionEpochReason::PoseBeforeInfo);
    f.burstInput(d); CHECK(!f.epoch().initializationEligible && f.fake->writes.empty());
    f.feed(pair(d)); f.feed(ack(d, burst[0]));
    auto other = d; other.mac[4] ^= 1; f.feed(ack(other, burst[1]));
    closed(f.epoch(), ConnectionEpochReason::DestinationConflict);
    CHECK(f.fake->writes.empty());
}
void legacyOrderingAndConfig() {
    const auto d = pairing_test::device();
    CHECK(BackendConfig{}.automaticVendorActionPolicy == AutomaticVendorActionPolicy::Legacy);
    Fixture f; std::size_t published = 0;
    f.fake->beforeWrite = [&] {
        CHECK(f.epoch().active);
        if (f.fake->writes.size() < 7) CHECK(published == 0 && f.epoch().expectedIndex == 0);
        if (f.fake->writes.size() == 7) CHECK(f.epoch().infoComplete && published == 9);
    };
    f.backend.setEventCallback([&](const auto& e) {
        if (e.kind == BackendEvent::Kind::DevicePaired) CHECK(f.fake->writes.size() == 7 && f.epoch().active);
        if (e.kind == BackendEvent::Kind::Ack && e.text == "NAZZ") CHECK(f.fake->writes.size() == 8 && f.epoch().infoComplete);
        ++published;
    });
    f.feed(pair(d)); f.burstInput(d);
    CHECK(f.fake->writes.size() == 8 && published == 10);
    f.fake->beforeWrite = {};
    f.backend.setEventCallback([&](const auto& e) {
        CHECK(e.kind == BackendEvent::Kind::DeviceUnpaired && f.fake->writes.size() == 8);
        closed(f.epoch(), ConnectionEpochReason::DeviceUnpaired); // callback before original PWS send
    });
    f.feed(pair(d, true)); CHECK(f.fake->writes.size() == 9);

    // Legacy bool disables configure only; FW1/PWS keep their original behavior
    // even when the independent collector rejects out-of-epoch/malformed NAZZ.
    BackendConfig config; config.configureTrackingOnPair = false;
    Fixture noConfigure(config); noConfigure.feed(pair(d)); CHECK(noConfigure.fake->writes.empty());
    noConfigure.feed(ack(d, "NAZZgarbage")); CHECK(noConfigure.fake->writes.size() == 1);
    closed(noConfigure.epoch(), ConnectionEpochReason::MalformedToken);
    noConfigure.feed(pair(d, true)); CHECK(noConfigure.fake->writes.size() == 2);
}
#endif
}
int main(int argc, char** argv) try {
    corruptEpochFixture = argc == 2 && std::string(argv[1]) == "--corrupt-epoch-fixture";
    happyAndFreshEpochs(); exactOrderFailures(); destinationAndGeneration(); shapesPoseAndUnrelated(); exhaustion();
#ifdef _WIN32
    observeOnlyAndExplicitOperations(); observeOnlyOverridesDiagnosticQueries(); backendFailureAndLifecycle(); legacyOrderingAndConfig();
#endif
    std::cout << "PASS transient epochs, exact profile burst, fail-closed ordering/destination/lifecycle, bounded counters and automatic-action policy; hardware NOT RUN\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
