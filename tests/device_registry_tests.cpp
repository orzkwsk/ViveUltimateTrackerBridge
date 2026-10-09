#include "vut/device_registry.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#define CHECK(x) do { if (!(x)) throw std::runtime_error("check failed: " #x); } while (false)
using namespace vut::control;

namespace vut::control {
struct DeviceRegistryTestAccess {
    static void epochExhausted(DeviceRegistry& r) { r.nextEpoch_ = r.limits_.counterCeiling; }
    static void revisionExhausted(DeviceRegistry& r) { r.revision_ = r.limits_.counterCeiling; }
    static void sessionExhausted(DeviceRegistry& r) {
        r.scope_.observationSession = r.limits_.counterCeiling;
        for (auto& record : r.records_) if (record.handle) record.binding.scope = r.scope_;
    }
    static void corruptSlot(DeviceRegistry& r) { r.slots_.begin()->second = 999; }
    static void duplicateOwner(DeviceRegistry& r) {
        auto copy = r.records_.front();
        copy.id += 100; copy.binding.connectionEpoch = copy.id;
        r.records_.push_back(copy);
    }
    static void candidate(DeviceRegistry& r) { r.records_.back().identity = IdentityState::Candidate; }
};
}
namespace {
constexpr RfAddress a{1, 2, 3, 4, 5, 6}, b{6, 5, 4, 3, 2, 1};
RuntimeHandle bind(DeviceRegistry& r, RfAddress rf = a, int slot = 0,
                   ConnectionState state = ConnectionState::Connected) {
    const auto result = r.bind(r.scope(), rf, slot, state);
    CHECK(result.ok() && result.value);
    return *result.value;
}
void stale(DeviceRegistry& r, RuntimeHandle h) {
    CHECK(r.validate(r.scope().serviceInstance, h).code == ErrorCode::StaleHandle);
    const auto revision = r.snapshot()->revision;
    CHECK(r.metadata(r.scope().serviceInstance, h).code == ErrorCode::StaleHandle);
    CHECK(r.disconnect(r.scope().serviceInstance, h).code == ErrorCode::StaleHandle);
    CHECK(r.snapshot()->revision == revision);
}
void replacementAndBoundaries() {
    std::int64_t now = 10;
    DeviceRegistry r([&] { return now; });
    const auto instance = r.scope().serviceInstance;
    CHECK(r.beginTransport(1).ok());
    auto h = bind(r);
    const auto original = r.snapshot();
    CHECK(original->devices.front().identity == IdentityState::Unavailable);
    CHECK(original->devices.front().identityReason == Reason::ProviderNotInstalled);
    const auto other = bind(r, b); // slot reuse without an unpair event
    stale(r, h);
    CHECK(r.validate(instance, other).ok());
    CHECK(original->devices.front().actionable && original->devices.front().runtimeHandle == h);
    auto snap = r.snapshot();
    CHECK(!snap->devices.front().actionable && !snap->devices.front().runtimeHandle);
    CHECK(snap->devices.front().identity == IdentityState::Stale);
    CHECK(snap->devices.back().binding.rf == b);
    CHECK(r.disconnect(instance, other).ok());
    stale(r, other);
    h = bind(r); // RF reused, same slot, still a new handle
    CHECK(h != original->devices.front().runtimeHandle);
    const auto epoch = r.snapshot()->devices.back().binding.connectionEpoch;
    CHECK(r.pose(instance, h, TrackingState::Tracking, 0x12, Admission::Admitted).ok());
    DeviceRegistryTestAccess::candidate(r); // fake evidence, no provider
    const auto moved = bind(r, a, 3);
    stale(r, h);
    snap = r.snapshot();
    CHECK(snap->devices.back().binding.connectionEpoch > epoch);
    CHECK(snap->devices.back().identity == IdentityState::Unavailable);
    CHECK(!snap->devices.back().lastAcceptedPoseAgeNs);
    const auto repeated = bind(r, a, 3);
    stale(r, moved);
    CHECK(repeated != moved);
    CHECK(r.snapshot()->devices.back().warning == Reason::None);
    CHECK(!r.snapshot()->devices.back().lastPoseReceiveAgeNs);
    const auto oldScope = r.scope();
    CHECK(r.restartObservationSession().ok());
    stale(r, repeated);
    CHECK(r.bind(oldScope, a, 3).outcome.code == ErrorCode::StaleHandle);
    const auto sessionHandle = bind(r);
    CHECK(r.beginTransport(2).ok());
    stale(r, sessionHandle);
    const auto generationHandle = bind(r);
    CHECK(r.loseTransport().ok());
    stale(r, generationHandle);
    CHECK(r.bind(r.scope(), b, 1).outcome.code == ErrorCode::TransportUnavailable);
    const auto rev = r.snapshot()->revision;
    CHECK(r.beginTransport(2).code == ErrorCode::InvalidArgument);
    CHECK(r.snapshot()->revision == rev);
    CHECK(r.beginTransport(3).ok());
    CHECK(bind(r) != generationHandle);

    DeviceRegistry restarted([&] { return now; });
    CHECK(restarted.scope().serviceInstance != instance);
    CHECK(restarted.beginTransport(1).ok()); // process-local generation reuse
    const auto newHandle = bind(restarted);
    CHECK(newHandle != h);
    CHECK(restarted.validate(instance, h).code == ErrorCode::StaleHandle);
    CHECK(restarted.validate(restarted.scope().serviceInstance, h).code == ErrorCode::StaleHandle);
    CHECK(r.validate(restarted.scope().serviceInstance, newHandle).code == ErrorCode::StaleHandle);
    CHECK(restarted.validate(instance, newHandle).code == ErrorCode::StaleHandle);
    stale(restarted, RuntimeHandle{}); // unknown token is never a slot selector
}
void receptionAndSnapshots() {
    std::int64_t now = 0;
    DeviceRegistry r([&] { return now; });
    CHECK(r.beginTransport(1).ok());
    const auto h = bind(r, a, 0, ConnectionState::Observed);
    const auto instance = r.scope().serviceInstance;
    CHECK(bind(r, a, 0, ConnectionState::Observed) == h);
    CHECK(r.snapshot()->devices.back().freshness == PoseFreshness::NeverObserved);
    CHECK(r.pose(instance, h, TrackingState::Tracking, 0x12, Admission::Admitted).ok());
    const auto immutable = r.snapshot({10, 7});
    now = 20;
    CHECK(r.metadata(instance, h).ok());
    auto d = r.snapshot({10, 7})->devices.front();
    CHECK(d.lastSeenAgeNs == 0 && d.lastPoseReceiveAgeNs == 20 && d.lastAcceptedPoseAgeNs == 20);
    CHECK(d.freshness == PoseFreshness::Stale && d.connection == ConnectionState::Observed);
    now = 30;
    CHECK(r.pose(instance, h, TrackingState::Lost, 0x03, Admission::Rejected).ok());
    d = r.snapshot({10, 7})->devices.front();
    CHECK(d.lastPoseReceiveAgeNs == 0 && d.lastAcceptedPoseAgeNs == 30);
    CHECK(d.tracking == TrackingState::Lost && d.trackingRaw == 0x03);
    CHECK(d.connection == ConnectionState::Observed && d.actionable);
    CHECK(r.snapshot()->devices.front().freshness == PoseFreshness::Unknown);
    CHECK(immutable->devices.front().lastAcceptedPoseAgeNs == 0);
    CHECK(immutable->devices.front().freshness == PoseFreshness::Fresh);
    const auto rev = r.snapshot()->revision;
    now = 40;
    CHECK(r.snapshot({10, 7})->revision == rev); // reading never refreshes evidence
    CHECK(r.snapshot({10, 7})->devices.front().lastAcceptedPoseAgeNs == 40);
    CHECK(r.pose(instance, h, TrackingState::Unknown, 0xff, Admission::Unconfigured).ok());
    CHECK(r.snapshot()->devices.front().lastAcceptedPoseAgeNs == 40);
    CHECK(r.disconnect(instance, h).ok());
    d = r.snapshot({100, 7})->devices.front();
    CHECK(!d.actionable && !d.runtimeHandle && d.freshness == PoseFreshness::Unknown);
}
void ambiguityAndInvalidInput() {
    DeviceRegistry r([] { return 10; });
    CHECK(r.beginTransport(1).ok());
    const auto h = bind(r), other = bind(r, b, 1);
    auto revision = r.snapshot()->revision;
    CHECK(r.bind(r.scope(), a, 8).outcome.code == ErrorCode::InvalidArgument);
    CHECK(r.bind(r.scope(), a, 0, ConnectionState::Disconnected).outcome.code == ErrorCode::InvalidArgument);
    CHECK(r.pose(r.scope().serviceInstance, h, static_cast<TrackingState>(999), 0, Admission::Admitted).code == ErrorCode::InvalidArgument);
    CHECK(r.snapshot()->revision == revision);
    const auto conflict = r.bind(r.scope(), a, 1); // two live owners: quarantine both
    CHECK(conflict.outcome.code == ErrorCode::Ambiguous && !conflict.value);
    stale(r, h); stale(r, other);
    const auto quarantined = r.snapshot();
    for (const auto& d : quarantined->devices) {
        CHECK(!d.actionable && !d.runtimeHandle);
        CHECK(d.identity == IdentityState::Ambiguous);
    }
    const auto fresh = bind(r);
    CHECK(r.quarantine(r.scope(), a, 0).code == ErrorCode::Ambiguous);
    stale(r, fresh);
    const auto poseFirst = bind(r);
    CHECK(r.bind(r.scope(), a, 3, ConnectionState::Observed).outcome.code == ErrorCode::Ambiguous);
    stale(r, poseFirst);
}
void boundedAndFaults() {
    DeviceRegistry bounded([] { return 1; }, {2, 5});
    CHECK(bounded.beginTransport(1).ok());
    const auto h = bind(bounded);
    CHECK(bind(bounded) != h);
    CHECK(bounded.bind(bounded.scope(), a, 0).outcome.reason == Reason::HistoryFull);
    auto snap = bounded.snapshot();
    CHECK(snap->faulted && snap->devices.size() == 2);
    for (const auto& d : snap->devices) CHECK(!d.actionable && !d.runtimeHandle);

    DeviceRegistry capacity([] { return 1; }, {256, 1});
    CHECK(capacity.beginTransport(1).ok());
    const auto current = bind(capacity);
    const auto rev = capacity.snapshot()->revision;
    CHECK(capacity.bind(capacity.scope(), b, 1).outcome.code == ErrorCode::Busy);
    CHECK(capacity.snapshot()->revision == rev && capacity.validate(capacity.scope().serviceInstance, current).ok());
    CHECK(bind(capacity, b, 0) != current); // replacement at current capacity

    for (int fault = 0; fault < 5; ++fault) {
        DeviceRegistry r([] { return 1; });
        CHECK(r.beginTransport(1).ok());
        const auto token = bind(r);
        if (fault == 0) DeviceRegistryTestAccess::epochExhausted(r);
        if (fault == 1) DeviceRegistryTestAccess::revisionExhausted(r);
        if (fault == 2) DeviceRegistryTestAccess::sessionExhausted(r);
        if (fault == 3) DeviceRegistryTestAccess::corruptSlot(r);
        if (fault == 4) DeviceRegistryTestAccess::duplicateOwner(r);
        if (fault >= 3) {
            CHECK(r.snapshot()->faulted && !r.snapshot()->devices.front().actionable);
            CHECK(r.validate(r.scope().serviceInstance, token).code == ErrorCode::InternalConsistencyError);
        } else {
            const auto error = fault == 2 ? r.restartObservationSession() : r.bind(r.scope(), b, 1).outcome;
            CHECK(error.code == ErrorCode::InternalConsistencyError && error.reason == Reason::CounterExhausted);
        }
        CHECK(r.snapshot()->faulted);
        CHECK(!r.snapshot()->devices.front().runtimeHandle);
        CHECK(!r.metadata(r.scope().serviceInstance, token).ok());
        CHECK(!r.beginTransport(2).ok()); // no silent recovery
    }
    std::int64_t now = 20;
    DeviceRegistry clockFailure([&] { return now; });
    CHECK(clockFailure.beginTransport(1).ok());
    const auto token = bind(clockFailure);
    now = 19;
    CHECK(clockFailure.snapshot()->faulted && !clockFailure.snapshot()->devices.front().runtimeHandle);
    CHECK(clockFailure.metadata(clockFailure.scope().serviceInstance, token).reason == Reason::InvalidTime);
    DeviceRegistry generation([] { return 1; }, {256, 5, 10});
    CHECK(generation.beginTransport(11).reason == Reason::CounterExhausted);
}
}
int main(int argc, char** argv) try {
    static_assert(!std::is_copy_constructible_v<DeviceRegistry>);
    static_assert(std::is_same_v<decltype(std::declval<DeviceRegistry>().snapshot()), std::shared_ptr<const RegistrySnapshot>>);
    if (argc == 2 && std::string_view(argv[1]) == "--negative-fixture") {
        DeviceRegistry r([] { return 1; });
        CHECK(r.beginTransport(1).ok());
        const auto retired = bind(r);
        (void)bind(r); // deliberately incorrect expectation proves Release CHECK is active
        CHECK(r.validate(r.scope().serviceInstance, retired).ok());
        return 0;
    }
    replacementAndBoundaries(); receptionAndSnapshots(); ambiguityAndInvalidInput(); boundedAndFaults();
    std::cout << "PASS registry ABA, namespaces, boundary retirement, ambiguity, ages, immutable snapshots, bounds and faults\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
