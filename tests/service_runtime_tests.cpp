#include "vut/service_runtime.hpp"
#include "vut/backend_transport.hpp"
#include "vut/protocol.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <type_traits>

#define CHECK(x) do { if (!(x)) throw std::runtime_error("check failed: " #x); } while (false)
using namespace vut;
using namespace vut::control;
namespace c2 = monaka::protocol::v2;
namespace vut {
struct ServiceRuntimeTestAccess {
    static void event(ServiceRuntime& runtime, const BackendEvent& event) { runtime.dispatch(event); }
};
}
namespace {
std::int64_t realNow() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
const DeviceId a{{0x23, 0x32, 0x42, 0xb7, 0x82, 0xd3}, 2};
const DeviceId b{{0x24, 0x31, 0x42, 0xb7, 0x82, 0xd3}, 1};
struct TransportEvidence {
    std::vector<std::vector<std::uint8_t>> writes;
    std::vector<std::string> lifecycle;
    std::function<void()> closing;
    bool dependentsDestroyed = false;
};
class FakeTransport final : public detail::BackendTransport {
    TransportEvidence& evidence_;
    std::uint8_t command_ = 0;
public:
    explicit FakeTransport(TransportEvidence& evidence) : evidence_(evidence) {}
    bool open(std::uint16_t, std::uint16_t, bool exclusive, std::string& error) override {
        ++opens; exclusiveOpen = exclusive;
        if (throwOpen) throw std::runtime_error("fake open exception");
        if (!openSucceeds) { error = "fake open unavailable"; return false; }
        closed = false; return true;
    }
    void close() override {
        if (!closed) {
            if (evidence_.closing && !evidence_.dependentsDestroyed) evidence_.closing();
            evidence_.lifecycle.push_back("stop");
        }
        closed = true;
    }
    std::size_t featureLength() const noexcept override { return 65; }
    bool setFeature(std::span<const std::uint8_t> bytes) override {
        CHECK(!closed);
        evidence_.writes.emplace_back(bytes.begin(), bytes.end()); command_ = bytes[1]; return true;
    }
    std::vector<std::uint8_t> getFeature() override {
        std::vector<std::uint8_t> response(65, 0); response[1] = command_; response[2] = 4; return response;
    }
    std::vector<std::uint8_t> read(int) override {
        ++readsCount;
        if (throwRead) throw std::runtime_error("fake read exception");
        if (reads.empty()) return {};
        auto report = std::move(reads.front()); reads.pop_front(); return report;
    }
    bool readFailed() const noexcept override { return failRead; }
    int opens = 0, readsCount = 0;
    bool closed = true, openSucceeds = true, throwOpen = false, throwRead = false, failRead = false;
    bool exclusiveOpen = false;
    std::deque<std::vector<std::uint8_t>> reads;
};
struct Probe {
    ServiceRuntime* runtime = nullptr;
    std::shared_ptr<const ServiceRuntimeSnapshot> beforeAdapter;
    bool throwAdmission = false, throwFactory = false, forceRestart = false, uncertain = false;
    bool checkReentrancy = false;
    int admissions = 0;
    TransportEvidence* evidence = nullptr;
};
// The test stage inspects real registry state at entry, then delegates to the
// ACTUAL adapter; ordering assertions do not rely on a mirrored fake codec.
class TracingStage final : public ServiceObservationStage {
    ViveObservationAdapter adapter_;
    Probe& probe_;
public:
    TracingStage(ObservationContext c, Probe& probe) : adapter_(std::move(c)), probe_(probe) {}
    ~TracingStage() override {
        probe_.evidence->lifecycle.push_back("dependent");
        probe_.evidence->dependentsDestroyed = true;
    }
    std::optional<c2::Envelope> admit(const BackendEvent& e) override {
        ++probe_.admissions;
        probe_.beforeAdapter = probe_.runtime->snapshot();
        if (probe_.checkReentrancy) {
            bool denied = false;
            try { (void)probe_.runtime->pollOnce(); } catch (const std::logic_error&) { denied = true; }
            CHECK(denied);
            denied = false;
            try { probe_.runtime->shutdown(); } catch (const std::logic_error&) { denied = true; }
            CHECK(denied);
        }
        if (probe_.throwAdmission) throw std::runtime_error("fake adapter exception");
        return adapter_.admit(e);
    }
    bool restartRequired() const override { return probe_.forceRestart || adapter_.restartRequired(); }
    bool coordinateContinuityUncertain() const override { return probe_.uncertain || adapter_.coordinateContinuityUncertain(); }
    std::uint64_t rejected() const override { return adapter_.rejected(); }
    const ObservationAdmissionDiagnostics& diagnostics() const override { return adapter_.diagnostics(); }
};
struct Fixture {
    TransportEvidence evidence;
    Probe probe;
    FakeTransport* fake = nullptr;
    std::int64_t now = 1000;
    bool realClock = false, failSend = false, throwSend = false;
    std::uint32_t revision = 0, sessions = 0;
    std::vector<std::string> bytes;
    std::unique_ptr<ServiceRuntime> runtime;
    Fixture(RegistryLimits limits = {}, ReconnectRevisionPolicy policy = ReconnectRevisionPolicy::Strict,
            std::size_t capacity = 512, bool real = false,
            AutomaticVendorActionPolicy vendor = AutomaticVendorActionPolicy::ObserveOnly) : realClock(real) {
        auto transport = std::make_unique<FakeTransport>(evidence); fake = transport.get();
        probe.evidence = &evidence;
        ServiceRuntimeConfig config;
        config.backend.automaticVendorActionPolicy = vendor;
        config.observation.sourceId = "vive-installation-a";
        config.observation.spaces = {{a.toString(), "map-a"}};
        config.registryLimits = limits; config.reconnectRevision = policy; config.publisherCapacity = capacity;
        config.diagnostics = true;
        ServiceRuntimeDependencies deps;
        deps.clock = [&] { return realClock ? realNow() : now; };
        deps.advanceRevision = [&] { return ++revision; };
        deps.makeSessionId = [&] {
            return "00000000-0000-4000-8000-" + std::string(11, '0') + std::to_string(++sessions);
        };
        deps.send = [&](std::string_view data) {
            if (throwSend) throw std::runtime_error("fake consumer exception");
            if (failSend) return false;
            bytes.emplace_back(data); return true;
        };
        deps.observationFactory = [&](ObservationContext c) -> std::unique_ptr<ServiceObservationStage> {
            if (probe.throwFactory) throw std::runtime_error("fake factory exception");
            evidence.dependentsDestroyed = false;
            return std::make_unique<TracingStage>(std::move(c), probe);
        };
        runtime = std::make_unique<ServiceRuntime>(Backend(std::move(transport)), std::move(config), std::move(deps));
        probe.runtime = runtime.get();
    }
    BackendEvent event(BackendEvent::Kind kind, DeviceId device = a, int packet = 0) {
        BackendEvent e{kind, device, {}, {}, ++now, runtime->snapshot()->observationSession->transportGeneration};
        if (kind == BackendEvent::Kind::Pose) {
            TrackerState p;
            p.device = device; p.packetIndex = static_cast<std::uint8_t>(packet);
            p.trackingStatus = TrackingStatus::Ok; p.trackingStatusRaw = 0x12;
            p.orientationXyzw.w = 1; p.observedMonotonicNs = e.observedMonotonicNs;
            e.pose = p;
        }
        return e;
    }
    void inject(BackendEvent e) { ServiceRuntimeTestAccess::event(*runtime, e); }
    bool deliver(BackendEvent e) { inject(std::move(e)); return runtime->pollOnce(); }
    RuntimeHandle current(std::size_t index = 0) {
        const auto snapshot = runtime->snapshot();
        CHECK(snapshot->control->registry.devices[index].runtimeHandle);
        return *snapshot->control->registry.devices[index].runtimeHandle;
    }
    Outcome validate(RuntimeHandle handle) {
        return runtime->validateHandle(runtime->snapshot()->control->registry.scope.serviceInstance, handle);
    }
};
void orderingAndAges() {
    Fixture f;
    CHECK(f.runtime->start().ok());
    CHECK(!f.fake->exclusiveOpen && f.evidence.writes.empty());
    f.probe.checkReentrancy = true;
    CHECK(f.deliver(f.event(BackendEvent::Kind::DevicePaired)));
    auto before = f.probe.beforeAdapter;
    CHECK(before->control->registry.devices.back().connection == ConnectionState::Connected);
    CHECK(before->control->registry.devices.back().lastSeenAgeNs == 0);
    CHECK(before->statistics.admission.acceptedState == 0);
    const auto token = f.current();
    CHECK(f.deliver(f.event(BackendEvent::Kind::Pose, a, 1)));
    CHECK(f.probe.beforeAdapter->control->registry.devices.front().lastPoseReceiveAgeNs == 0);
    CHECK(!f.probe.beforeAdapter->control->registry.devices.front().lastAcceptedPoseAgeNs);
    auto device = f.runtime->snapshot()->control->registry.devices.front();
    CHECK(device.lastAcceptedPoseAgeNs == 0 && device.admission == Admission::Admitted);
    const auto acceptedSnapshot = f.runtime->snapshot();
    CHECK(f.deliver(f.event(BackendEvent::Kind::Pose, a, 1))); // duplicate RF rejected
    device = f.runtime->snapshot()->control->registry.devices.front();
    CHECK(device.lastPoseReceiveAgeNs == 0 && device.lastAcceptedPoseAgeNs == 1);
    CHECK(device.admission == Admission::Rejected);
    CHECK(f.deliver(f.event(BackendEvent::Kind::Ack)));
    device = f.runtime->snapshot()->control->registry.devices.front();
    CHECK(device.lastSeenAgeNs == 0 && device.lastPoseReceiveAgeNs == 1 && device.lastAcceptedPoseAgeNs == 2);
    for (int malformed = 0; malformed < 3; ++malformed) {
        auto rejected = f.event(BackendEvent::Kind::Pose, a, 2);
        if (malformed == 0) --rejected.pose->observedMonotonicNs;
        if (malformed == 1) rejected.pose.reset();
        if (malformed == 2) rejected.pose->device = b;
        CHECK(f.deliver(rejected));
        device = f.runtime->snapshot()->control->registry.devices.front();
        CHECK(device.lastPoseReceiveAgeNs == 0 && device.lastAcceptedPoseAgeNs == malformed + 3);
        CHECK(device.admission == Admission::Rejected);
    }
    CHECK(f.deliver(f.event(BackendEvent::Kind::Pose, b, 1))); // unconfigured, distinct slot
    device = f.runtime->snapshot()->control->registry.devices.back();
    CHECK(device.connection == ConnectionState::Observed && device.lastPoseReceiveAgeNs == 0);
    CHECK(device.admission == Admission::Unconfigured && !device.lastAcceptedPoseAgeNs);
    CHECK(device.identity == IdentityState::Unavailable && device.identityReason == Reason::ProviderNotInstalled);
    CHECK(f.deliver(f.event(BackendEvent::Kind::DeviceUnpaired)));
    CHECK(!f.probe.beforeAdapter->control->registry.devices.front().actionable);
    CHECK(f.validate(token).code == ErrorCode::StaleHandle);
    CHECK(acceptedSnapshot->control->registry.devices.front().actionable);
    CHECK(acceptedSnapshot->control->registry.devices.front().lastAcceptedPoseAgeNs == 0);
    const auto revision = f.runtime->snapshot()->control->registry.revision;
    ++f.now;
    CHECK(f.runtime->snapshot()->control->registry.revision == revision);
    CHECK(f.runtime->snapshot()->statistics.callbackRegistrations == 1);
    CHECK(!ServiceRuntimeSnapshot::pairingMutation && !ServiceRuntimeSnapshot::identifyMutation && !ServiceRuntimeSnapshot::identityAcquisition);
}
void bindingBoundaries() {
    Fixture f;
    CHECK(f.runtime->start().ok());
    CHECK(f.deliver(f.event(BackendEvent::Kind::DevicePaired)));
    auto old = f.current();
    CHECK(f.deliver(f.event(BackendEvent::Kind::DevicePaired)));
    CHECK(f.validate(old).code == ErrorCode::StaleHandle);
    auto repeated = f.current(1);
    CHECK(repeated != old);
    DeviceId replacement = b; replacement.slot = a.slot; // synthetic boundary, not vendor/HIL evidence
    CHECK(f.deliver(f.event(BackendEvent::Kind::DevicePaired, replacement)));
    CHECK(f.validate(repeated).code == ErrorCode::StaleHandle);
    auto occupant = f.current(2);
    CHECK(f.deliver(f.event(BackendEvent::Kind::DeviceUnpaired, a))); // late old RF cannot retire B
    CHECK(f.validate(occupant).ok());
    DeviceId moved = replacement; moved.slot = 3;
    CHECK(f.deliver(f.event(BackendEvent::Kind::DevicePaired, moved)));
    CHECK(f.validate(occupant).code == ErrorCode::StaleHandle);
    old = f.current(3);
    DeviceId observedMoved = moved; observedMoved.slot = 4;
    CHECK(f.deliver(f.event(BackendEvent::Kind::Pose, observedMoved, 1)));
    CHECK(f.validate(old).code == ErrorCode::StaleHandle); // uncertain pose-first move quarantines
    CHECK(!f.runtime->snapshot()->control->registry.devices.back().runtimeHandle);
}
void transportAndSession() {
    for (auto policy : {ReconnectRevisionPolicy::Strict, ReconnectRevisionPolicy::PreserveOnTransportReconnect}) {
        Fixture f({}, policy);
        CHECK(f.runtime->start().ok());
        CHECK(f.deliver(f.event(BackendEvent::Kind::DevicePaired)));
        auto token = f.current();
        auto late = f.event(BackendEvent::Kind::Pose, a, 1);
        const auto first = f.runtime->snapshot();
        f.fake->failRead = true;
        CHECK(!f.runtime->pollOnce());
        CHECK(f.runtime->snapshot()->state == ServiceRuntimeState::TransportLost);
        CHECK(f.validate(token).code == ErrorCode::StaleHandle && f.fake->closed);
        CHECK(f.runtime->snapshot()->statistics.pendingPublication == 0);
        f.fake->failRead = false;
        CHECK(f.runtime->start().ok());
        auto second = f.runtime->snapshot();
        CHECK(second->observationSession->sessionId != first->observationSession->sessionId);
        CHECK(second->observationSession->transportGeneration > first->observationSession->transportGeneration);
        CHECK(second->observationSession->revision == (policy == ReconnectRevisionPolicy::Strict ? 2 : 1));
        CHECK(second->control->registry.scope.observationSession != first->control->registry.scope.observationSession);
        const auto modelRevision = second->control->registry.revision;
        f.inject(late); CHECK(f.runtime->pollOnce());
        CHECK(f.runtime->snapshot()->control->registry.revision == modelRevision);
        CHECK(f.runtime->snapshot()->statistics.admission.rejectGeneration == 1);
        auto future = f.event(BackendEvent::Kind::Pose, a, 99);
        future.observedMonotonicNs = f.now + 100;
        future.pose->observedMonotonicNs = future.observedMonotonicNs;
        const auto beforeFuture = f.runtime->snapshot()->control->registry.revision;
        CHECK(f.deliver(future));
        CHECK(f.runtime->snapshot()->control->registry.revision == beforeFuture);
        CHECK(f.runtime->statistics().admission.acceptedPose == 0);
        CHECK(f.runtime->snapshot()->statistics.callbackRegistrations == 1);
        CHECK(f.deliver(f.event(BackendEvent::Kind::DevicePaired)));
        token = f.current(1);
        CHECK(f.deliver(f.event(BackendEvent::Kind::DeviceUnpaired)));
        const auto beforeRestart = f.runtime->snapshot();
        CHECK(!f.deliver(f.event(BackendEvent::Kind::DevicePaired))); // real adapter restartRequired
        CHECK(f.runtime->snapshot()->state == ServiceRuntimeState::RestartRequired);
        CHECK(f.validate(token).code == ErrorCode::StaleHandle);
        CHECK(f.runtime->snapshot()->control->registry.scope.observationSession != beforeRestart->control->registry.scope.observationSession);
        CHECK(f.runtime->snapshot()->statistics.pendingPublication == 0);
        const auto controls = f.runtime->snapshot()->control->registry.revision;
        f.inject(late); CHECK(f.runtime->snapshot()->control->registry.revision == controls);
        CHECK(f.runtime->start().ok());
        CHECK(f.runtime->snapshot()->observationSession->revision == (policy == ReconnectRevisionPolicy::Strict ? 3 : 1));
        f.probe.forceRestart = true; f.probe.uncertain = true;
        CHECK(!f.runtime->pollOnce());
        f.probe.forceRestart = false; f.probe.uncertain = false;
        CHECK(f.runtime->start().ok());
        CHECK(f.runtime->snapshot()->observationSession->revision == (policy == ReconnectRevisionPolicy::Strict ? 4 : 2));
    }
}
void shutdownAndFailures() {
    for (int failure = 0; failure < 6; ++failure) {
        Fixture f({}, ReconnectRevisionPolicy::Strict, failure == 3 ? 0 : 512, failure == 1);
        if (failure == 2) f.probe.throwFactory = true;
        if (failure == 4) f.fake->throwOpen = true;
        f.evidence.closing = [&] {
            const auto snapshot = f.runtime->snapshot();
            CHECK(snapshot->statistics.callbackClears == 1);
            for (const auto& device : snapshot->control->registry.devices) CHECK(!device.runtimeHandle);
            CHECK(!f.evidence.dependentsDestroyed);
            f.evidence.lifecycle.push_back("clear-before-stop");
        };
        if (failure >= 2 && failure <= 4) {
            CHECK(!f.runtime->start().ok());
            CHECK(f.runtime->snapshot()->state == ServiceRuntimeState::Faulted);
        } else {
            CHECK(f.runtime->start().ok());
            if (failure == 1) {
                f.probe.throwAdmission = true;
                std::vector<std::uint8_t> report(12, 0); report[0] = protocol::kResponsePairEvent; report[1] = 12;
                std::copy(a.mac.begin(), a.mac.end(), report.begin()+6); f.fake->reads.push_back(report);
                CHECK(!f.runtime->pollOnce());
                CHECK(f.runtime->snapshot()->failure == ServiceRuntimeFailure::Dispatcher);
            } else if (failure == 5) {
                f.fake->throwRead = true;
                CHECK(!f.runtime->pollOnce());
                CHECK(f.runtime->snapshot()->failure == ServiceRuntimeFailure::Poll);
            } else {
                CHECK(f.deliver(f.event(BackendEvent::Kind::DevicePaired)));
                f.runtime->shutdown();
            }
        }
        CHECK(f.fake->closed);
        CHECK(f.runtime->snapshot()->statistics.callbackClears == 1);
        f.runtime->shutdown(); // idempotent
        CHECK(f.runtime->snapshot()->statistics.callbackClears == 1);
        const auto stop = std::find(f.evidence.lifecycle.begin(), f.evidence.lifecycle.end(), "stop");
        const auto dependent = std::find(f.evidence.lifecycle.begin(), f.evidence.lifecycle.end(), "dependent");
        if (dependent != f.evidence.lifecycle.end()) CHECK(stop < dependent);
        CHECK(f.evidence.writes.empty()); // ObserveOnly includes exceptional teardown
    }
    Fixture unavailable;
    unavailable.fake->openSucceeds = false;
    CHECK(unavailable.runtime->start().code == ErrorCode::TransportUnavailable);
    CHECK(unavailable.runtime->snapshot()->state == ServiceRuntimeState::TransportLost);
    unavailable.fake->openSucceeds = true;
    CHECK(unavailable.runtime->start().ok());
    CHECK(unavailable.runtime->snapshot()->statistics.callbackRegistrations == 1);
    std::atomic_bool denied{false};
    std::thread wrongOwner([&] {
        try { (void)unavailable.runtime->pollOnce(); }
        catch (const std::logic_error&) { denied = true; }
    });
    wrongOwner.join(); CHECK(denied && unavailable.fake->readsCount == 0);

    Fixture regressed;
    regressed.fake->openSucceeds = false;
    CHECK(regressed.runtime->start().code == ErrorCode::TransportUnavailable);
    --regressed.now;
    CHECK(regressed.runtime->start().reason == Reason::InvalidTime);
    CHECK(regressed.runtime->snapshot()->state == ServiceRuntimeState::Faulted);
    CHECK(regressed.runtime->snapshot()->failure == ServiceRuntimeFailure::Registry);
}
void exhaustionAndFlood() {
    Fixture f({2, 5});
    CHECK(f.runtime->start().ok());
    CHECK(f.deliver(f.event(BackendEvent::Kind::DevicePaired)));
    CHECK(f.deliver(f.event(BackendEvent::Kind::DevicePaired)));
    CHECK(!f.deliver(f.event(BackendEvent::Kind::DevicePaired)));
    const auto failed = f.runtime->snapshot();
    CHECK(failed->state == ServiceRuntimeState::Faulted && failed->failure == ServiceRuntimeFailure::Registry);
    CHECK(failed->outcome.reason == Reason::HistoryFull && failed->control->registry.faulted);
    CHECK(failed->control->registry.devices.size() == 2);
    for (const auto& device : failed->control->registry.devices) CHECK(!device.actionable && !device.runtimeHandle);
    const auto opens = f.fake->opens;
    CHECK(f.runtime->start().reason == Reason::HistoryFull && f.fake->opens == opens);
    CHECK(f.runtime->snapshot()->control->registry.scope.serviceInstance == failed->control->registry.scope.serviceInstance);

    Fixture defaultLimit;
    CHECK(defaultLimit.runtime->start().ok());
    for (int binding = 0; binding < 256; ++binding)
        CHECK(defaultLimit.deliver(defaultLimit.event(BackendEvent::Kind::DevicePaired)));
    CHECK(!defaultLimit.deliver(defaultLimit.event(BackendEvent::Kind::DevicePaired)));
    CHECK(defaultLimit.runtime->snapshot()->outcome.reason == Reason::HistoryFull);
    CHECK(defaultLimit.runtime->snapshot()->control->registry.devices.size() == 256);

    Fixture flood;
    CHECK(flood.runtime->start().ok());
    // Pose first creates exactly one Observed binding. Snapshot only at endpoints;
    // the production dispatch does no full registry copy or history scan for lookup.
    for (int i = 0; i < 2000; ++i) CHECK(flood.deliver(flood.event(BackendEvent::Kind::Pose, a, i)));
    auto snapshot = flood.runtime->snapshot();
    CHECK(snapshot->control->registry.devices.size() == 1);
    CHECK(snapshot->control->registry.devices.front().connection == ConnectionState::Observed);
    CHECK(snapshot->statistics.publication.sent == 2000 && snapshot->statistics.pendingPublication == 0);
    flood.failSend = true;
    CHECK(flood.deliver(flood.event(BackendEvent::Kind::Pose, a, 2000)));
    flood.throwSend = true;
    CHECK(flood.deliver(flood.event(BackendEvent::Kind::Pose, a, 2001)));
    CHECK(flood.runtime->snapshot()->statistics.publication.sendErrors == 2);
    CHECK(flood.runtime->snapshot()->state == ServiceRuntimeState::Running);
    CHECK(flood.evidence.writes.empty());
}
void observationBytesEquivalent() {
    Fixture f;
    CHECK(f.runtime->start().ok());
    const auto session = *f.runtime->snapshot()->observationSession;
    ObservationContext context{"vive-installation-a", session.sessionId, session.transportGeneration,
        session.revision, session.epochNs, {{a.toString(), "map-a"}}};
    ViveObservationAdapter legacy(context);
    std::vector<std::string> bytes;
    ObservationPublisher publisher(session.sessionId, [&](std::string_view s) { bytes.emplace_back(s); return true; },
        [&] { return f.now - session.epochNs; });
    const auto deliver = [&](BackendEvent e) {
        if (auto admitted = legacy.admit(e)) publisher.enqueue(std::move(*admitted));
        publisher.flush();
        const bool active = f.deliver(std::move(e));
        CHECK(active == !legacy.restartRequired());
        CHECK(bytes == f.bytes);
    };
    deliver(f.event(BackendEvent::Kind::DevicePaired));
    int packetIndex = 0;
    for (auto status : {TrackingStatus::Ok, TrackingStatus::Lost, TrackingStatus::RecentlyLost, TrackingStatus::Unknown}) {
        auto e = f.event(BackendEvent::Kind::Pose, a, packetIndex++);
        e.pose->trackingStatus = status; e.pose->trackingStatusRaw = static_cast<std::uint8_t>(status);
        deliver(e);
        deliver(e); // duplicate timestamp rejection unchanged
    }
    auto duplicate = f.event(BackendEvent::Kind::Pose, a, packetIndex-1); deliver(duplicate);
    auto invalid = f.event(BackendEvent::Kind::Pose, a, packetIndex++);
    invalid.pose->orientationXyzw.w = std::numeric_limits<float>::infinity(); deliver(invalid);
    for (const auto packet : {122, 240, 255, 0, 255}) deliver(f.event(BackendEvent::Kind::Pose, a, packet));
    f.now += 500000001;
    deliver(f.event(BackendEvent::Kind::Pose, a, 1)); // existing RF-gap rejection
    deliver(f.event(BackendEvent::Kind::Pose, b, 1)); // unconfigured
    deliver(f.event(BackendEvent::Kind::Ack));
    deliver(f.event(BackendEvent::Kind::DeviceUnpaired));
    deliver(f.event(BackendEvent::Kind::DevicePaired));
    publisher.clear();
    CHECK(legacy.diagnostics().acceptedPose == f.runtime->statistics().admission.acceptedPose);
    CHECK(legacy.rejected() == f.runtime->statistics().rejected);
    CHECK(f.runtime->statistics().pendingPublication == 0);
}
std::vector<std::uint8_t> pairReport(bool unpair = false) {
    std::vector<std::uint8_t> report(12, 0);
    report[0] = protocol::kResponsePairEvent; report[1] = 12; report[5] = unpair;
    std::copy(a.mac.begin(), a.mac.end(), report.begin()+6); return report;
}
std::vector<std::uint8_t> ackReport() {
    std::vector<std::uint8_t> report(16, 0);
    report[0] = protocol::kResponseTrackerIncoming;
    std::copy(a.mac.begin(), a.mac.end(), report.begin()+3);
    report[9] = static_cast<std::uint8_t>(protocol::kIncomingAck);
    report[10] = static_cast<std::uint8_t>(protocol::kIncomingAck >> 8);
    report[11] = 4; std::copy_n("NAZZ", 4, report.begin()+12); return report;
}
void commandSequenceEquivalent() {
    std::ifstream stream(std::string(FIXTURE_DIR) + "/pose37.bin", std::ios::binary);
    std::vector<std::uint8_t> pose{std::istreambuf_iterator<char>(stream), {}};
    CHECK(!pose.empty());
    const std::deque<std::vector<std::uint8_t>> reports{pairReport(), ackReport(), pose, pairReport(true)};
    TransportEvidence before;
    auto transport = std::make_unique<FakeTransport>(before);
    auto* raw = transport.get(); raw->reads = reports;
    Backend legacy(std::move(transport));
    CHECK(legacy.start());
    while (!raw->reads.empty()) CHECK(legacy.pollOnce());
    legacy.stop();
    Fixture after({}, ReconnectRevisionPolicy::Strict, 512, true, AutomaticVendorActionPolicy::Legacy);
    after.fake->reads = reports;
    CHECK(after.runtime->start().ok());
    while (!after.fake->reads.empty()) CHECK(after.runtime->pollOnce());
    after.runtime->shutdown();
    CHECK(before.writes == after.evidence.writes && !before.writes.empty());
    CHECK(!after.fake->exclusiveOpen);
    int fw1 = 0, pws = 0;
    for (const auto& packet : after.evidence.writes) {
        if (packet[1] == protocol::kCmdRequestRfChangeBehavior) {
            CHECK(packet[3] == protocol::kRfBehaviorRxPowerSaving && packet[4] == 0); ++pws;
        } else {
            CHECK(packet[1] == protocol::kCmdTx);
            const auto text = std::string(packet.begin()+13, packet.begin()+13+packet[12]);
            CHECK(text != "NA" && text != "RestartRf");
            if (text == "FW1") ++fw1;
        }
    }
    CHECK(fw1 == 1 && pws == 1);
}
const std::array<std::string_view, 9> infoBurst{
    "NANAABC", "NADSABCDEFGHIJKL", "NASSABCDEFGHIJKLM", "NASI12345678",
    "NAPIABCDEFGH", "NAV1ABCDEFGHIJK", "NARI1", "NAGNABCDE", "NAZZ"
};
ConnectionEpochSnapshot epoch(const ServiceRuntimeSnapshot& snapshot, int slot = a.slot) {
    const auto found = std::find_if(snapshot.connectionEpochs.rbegin(), snapshot.connectionEpochs.rend(),
        [slot](const auto& e) { return e.slot == slot; });
    CHECK(found != snapshot.connectionEpochs.rend()); return *found;
}
void collect(Fixture& f, DeviceId device = a) {
    for (auto text : infoBurst) {
        auto e = f.event(BackendEvent::Kind::Ack, device); e.text = text; CHECK(f.deliver(e));
    }
}
void assertInvalid(const ServiceRuntimeSnapshot& s, ConnectionEpochReason reason, int slot = a.slot) {
    const auto e = epoch(s, slot);
    CHECK(!e.active && !e.infoComplete && !e.nadsFresh && !e.nassFresh && !e.deviceCandidate);
    CHECK(e.reason == reason);
}
void epochOrderingAndInvalidation() {
    Fixture f;
    CHECK(f.runtime->start().ok());
    CHECK(f.deliver(f.event(BackendEvent::Kind::DevicePaired)));
    auto before = f.probe.beforeAdapter;
    CHECK(epoch(*before).active && !epoch(*before).nadsPresent);
    CHECK(before->control->registry.devices.back().runtimeHandle);
    const auto handle = f.current();
    collect(f);
    before = f.probe.beforeAdapter;
    auto e = epoch(*before);
    CHECK(e.infoComplete && e.nadsPresent && e.nassPresent && e.nadsFresh && e.nassFresh);
    CHECK(e.deviceCandidate && e.deviceCandidate->mac == a.mac && e.deviceCandidate->slot == a.slot);
    CHECK(e.transportGeneration == before->control->registry.scope.transportGeneration);
    CHECK(before->control->registry.devices.back().identity == IdentityState::Unavailable);
    CHECK(before->control->registry.devices.back().identityReason == Reason::ProviderNotInstalled);
    const auto detached = before;
    CHECK(f.deliver(f.event(BackendEvent::Kind::DeviceUnpaired)));
    assertInvalid(*f.probe.beforeAdapter, ConnectionEpochReason::DeviceUnpaired);
    CHECK(!f.probe.beforeAdapter->control->registry.devices.back().runtimeHandle);
    CHECK(f.validate(handle).code == ErrorCode::StaleHandle);
    CHECK(epoch(*detached).infoComplete && detached->control->registry.devices.back().runtimeHandle);

    Fixture reopened;
    CHECK(reopened.runtime->start().ok());
    CHECK(reopened.deliver(reopened.event(BackendEvent::Kind::DevicePaired))); collect(reopened);
    auto stale = reopened.event(BackendEvent::Kind::Ack); stale.text = "NANAABC";
    const auto oldEpoch = epoch(*reopened.runtime->snapshot());
    reopened.fake->failRead = true; CHECK(!reopened.runtime->pollOnce());
    assertInvalid(*reopened.runtime->snapshot(), ConnectionEpochReason::TransportLost);
    const auto opens = reopened.fake->opens;
    CHECK(!reopened.runtime->pollOnce() && reopened.fake->opens == opens); // no automatic reopen
    reopened.fake->failRead = false; CHECK(reopened.runtime->start().ok());
    CHECK(reopened.deliver(reopened.event(BackendEvent::Kind::DevicePaired)));
    reopened.inject(stale); CHECK(reopened.runtime->pollOnce());
    e = epoch(*reopened.runtime->snapshot());
    CHECK(e.transportGeneration > oldEpoch.transportGeneration && e.connectionEpochId > oldEpoch.connectionEpochId);
    CHECK(e.active && !e.nadsFresh && e.seenMask == 0); collect(reopened);
    reopened.probe.forceRestart = true; CHECK(!reopened.runtime->pollOnce());
    assertInvalid(*reopened.runtime->snapshot(), ConnectionEpochReason::ObservationSessionRestarted);
    reopened.probe.forceRestart = false; CHECK(reopened.runtime->start().ok());
    auto tail = reopened.event(BackendEvent::Kind::Ack); tail.text = "NAZZ";
    CHECK(reopened.deliver(tail)); CHECK(!epoch(*reopened.runtime->snapshot()).infoComplete);
    CHECK(reopened.deliver(reopened.event(BackendEvent::Kind::DevicePaired)));
    CHECK(epoch(*reopened.runtime->snapshot()).seenMask == 0); collect(reopened);
    reopened.runtime->shutdown(); assertInvalid(*reopened.runtime->snapshot(), ConnectionEpochReason::RuntimeShutdown);
    CHECK(reopened.evidence.writes.empty() && f.evidence.writes.empty());
}
void epochOwnershipAndMutation() {
    Fixture f; CHECK(f.runtime->start().ok());
    CHECK(f.deliver(f.event(BackendEvent::Kind::DevicePaired))); collect(f);
    auto replacement = b; replacement.slot = a.slot;
    CHECK(f.deliver(f.event(BackendEvent::Kind::DevicePaired, replacement)));
    CHECK(!epoch(*f.runtime->snapshot()).nadsFresh); collect(f, replacement);
    CHECK(f.deliver(f.event(BackendEvent::Kind::DeviceUnpaired, a)));
    CHECK(epoch(*f.runtime->snapshot()).infoComplete); // late old unpair cannot retire replacement
    auto moved = replacement; moved.slot = 3;
    CHECK(f.deliver(f.event(BackendEvent::Kind::DevicePaired, moved)));
    assertInvalid(*f.probe.beforeAdapter, ConnectionEpochReason::AddressMoved);
    CHECK(!epoch(*f.probe.beforeAdapter, 3).nadsFresh); collect(f, moved);
    auto uncertain = moved; uncertain.slot = 4;
    CHECK(f.deliver(f.event(BackendEvent::Kind::Pose, uncertain, 1)));
    assertInvalid(*f.probe.beforeAdapter, ConnectionEpochReason::AddressMoved, 3);
    CHECK(!f.probe.beforeAdapter->control->registry.devices.back().runtimeHandle);

    Fixture ambiguity; CHECK(ambiguity.runtime->start().ok());
    CHECK(ambiguity.deliver(ambiguity.event(BackendEvent::Kind::DevicePaired))); collect(ambiguity);
    CHECK(ambiguity.deliver(ambiguity.event(BackendEvent::Kind::DevicePaired, b))); collect(ambiguity, b);
    auto conflict = a; conflict.slot = b.slot; // RF owns a different current slot
    CHECK(ambiguity.deliver(ambiguity.event(BackendEvent::Kind::DevicePaired, conflict)));
    assertInvalid(*ambiguity.probe.beforeAdapter, ConnectionEpochReason::RegistryOwnershipAmbiguous, b.slot);
    for (const auto& record : ambiguity.probe.beforeAdapter->control->registry.devices) CHECK(!record.runtimeHandle);

    for (const auto token : {"NADSABCDEFGHIJKL", "NADSABCDEFGHIJKX"}) {
        Fixture duplicate; CHECK(duplicate.runtime->start().ok());
        CHECK(duplicate.deliver(duplicate.event(BackendEvent::Kind::DevicePaired))); collect(duplicate);
        auto repeated = duplicate.event(BackendEvent::Kind::Ack); repeated.text = token;
        CHECK(duplicate.deliver(repeated)); assertInvalid(*duplicate.probe.beforeAdapter, ConnectionEpochReason::DuplicateToken);
        collect(duplicate); CHECK(!epoch(*duplicate.runtime->snapshot()).nadsFresh);
    }
}
std::vector<std::uint8_t> infoReport(std::string_view text) {
    auto p = ackReport(); p.resize(12 + text.size()); p[11] = static_cast<std::uint8_t>(text.size());
    std::copy(text.begin(), text.end(), p.begin()+12); return p;
}
void observeOnlyRealBackendNoSend() {
    CHECK(ServiceRuntimeConfig{}.backend.automaticVendorActionPolicy == AutomaticVendorActionPolicy::ObserveOnly);
    Fixture f({}, ReconnectRevisionPolicy::Strict, 512, true);
    CHECK(f.runtime->start().ok() && f.evidence.writes.empty());
    f.fake->reads.push_back(pairReport());
    for (auto token : infoBurst) f.fake->reads.push_back(infoReport(token));
    while (!f.fake->reads.empty()) CHECK(f.runtime->pollOnce());
    CHECK(epoch(*f.probe.beforeAdapter).infoComplete && f.evidence.writes.empty());
    f.fake->reads.push_back(pairReport(true)); CHECK(f.runtime->pollOnce());
    assertInvalid(*f.probe.beforeAdapter, ConnectionEpochReason::DeviceUnpaired);
    f.fake->failRead = true; CHECK(!f.runtime->pollOnce());
    CHECK(f.evidence.writes.empty()); f.fake->failRead = false; CHECK(f.runtime->start().ok());
    f.runtime->shutdown(); CHECK(f.evidence.writes.empty());
}
}
int main() try {
    static_assert(!std::is_copy_constructible_v<ServiceRuntime> && !std::is_move_constructible_v<ServiceRuntime>);
    static_assert(std::is_same_v<decltype(std::declval<ServiceRuntime>().snapshot()), std::shared_ptr<const ServiceRuntimeSnapshot>>);
    orderingAndAges(); bindingBoundaries(); transportAndSession(); shutdownAndFailures();
    exhaustionAndFlood(); observationBytesEquivalent(); commandSequenceEquivalent();
    epochOrderingAndInvalidation(); epochOwnershipAndMutation(); observeOnlyRealBackendNoSend();
    std::cout << "PASS passive runtime ordering, admission ages, ABA, sessions, shutdown, exceptions, exhaustion, flood, exact bytes and command equivalence\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
