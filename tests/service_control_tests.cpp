#include "vut/service_control.hpp"

#include <iostream>
#include <stdexcept>
#include <type_traits>

#define CHECK(x) do { if (!(x)) throw std::runtime_error("check failed: " #x); } while (false)
using namespace vut::control;
namespace {
constexpr RfAddress a{1, 2, 3, 4, 5, 6}, b{6, 5, 4, 3, 2, 1};
const Outcome failed{ErrorCode::OperationFailed, Reason::PrimaryFailure};
const Outcome cleanupFailed{ErrorCode::CleanupUnconfirmed, Reason::CleanupFailure};
RuntimeHandle bind(ServiceControlModel& m, RfAddress rf = a, int slot = 0) {
    const auto result = m.bind(m.scope(), rf, slot);
    CHECK(result.ok() && result.value); return *result.value;
}
OperationId enqueue(ServiceControlModel& m, std::optional<RuntimeHandle> h = {}) {
    const auto result = m.enqueue(m.scope().serviceInstance, OperationKind::Pairing, h);
    CHECK(result.ok() && result.value); return *result.value;
}
void primaryAndCleanup() {
    std::int64_t now = 10;
    ServiceControlModel m([&] { return now; });
    CHECK(m.beginTransport(1).ok());
    const auto handle = bind(m);
    const auto id = enqueue(m, handle);
    auto original = m.snapshot();
    CHECK(original->operations.front().createdAtNs == 10);
    CHECK(original->registry.devices.front().operationIds == std::vector<OperationId>{id});
    now = 20;
    CHECK(m.start(id).ok());
    CHECK(m.requireCleanup(id).ok());
    CHECK(m.complete(id, OperationState::Failed, failed).ok());
    now = 30;
    CHECK(m.recordCleanup(id, cleanupFailed).ok());
    auto record = m.snapshot()->operations.front();
    CHECK(record.primaryResult == failed && record.cleanupResult == cleanupFailed && record.firstCleanupFailure == cleanupFailed);
    CHECK(record.cleanup == CleanupState::Unconfirmed && record.cleanupAttempts == 1);
    CHECK(record.createdAtNs == 10 && record.updatedAtNs == 30);
    CHECK(m.enqueue(m.scope().serviceInstance, OperationKind::Identify).outcome.code == ErrorCode::CleanupUnconfirmed);
    CHECK(m.expire(id).code == ErrorCode::Busy);
    CHECK(m.recordCleanup(id, {ErrorCode::CleanupUnconfirmed, Reason::Deadline}).ok());
    record = m.snapshot()->operations.front();
    CHECK(record.firstCleanupFailure == cleanupFailed && record.cleanupResult->reason == Reason::Deadline);
    CHECK(m.loseTransport().ok());
    CHECK(m.beginTransport(2).ok());
    now = 40;
    CHECK(m.recordCleanup(id, {}).ok()); // verified future cleanup input, never HID
    record = m.snapshot()->operations.front();
    CHECK(record.state == OperationState::Failed && record.primaryResult == failed);
    CHECK(record.firstCleanupFailure == cleanupFailed && record.cleanup == CleanupState::Confirmed);
    CHECK(record.cleanupResult && record.cleanupResult->ok());
    CHECK(record.cleanupAttempts == 3);
    const auto revision = m.snapshot()->registry.revision;
    CHECK(m.complete(id, OperationState::Cancelled, {ErrorCode::OperationFailed, Reason::Cancelled}).code == ErrorCode::InvalidArgument);
    CHECK(m.recordCleanup(id, {}).code == ErrorCode::InvalidArgument);
    CHECK(m.snapshot()->registry.revision == revision);
    CHECK(original->operations.front().state == OperationState::Queued && !original->operations.front().primaryResult);
    CHECK(original->registry.devices.front().actionable);
    CHECK(m.expire(id).ok());
    CHECK(m.snapshot()->operations.empty());
    CHECK(m.snapshot()->registry.devices.front().operationIds.empty());
    CHECK(m.start(id).code == ErrorCode::Unavailable);
    CHECK(enqueue(m) > id);
}
void lifecycleAndQueue() {
    ServiceControlModel m([] { return 10; });
    CHECK(m.enqueue(m.scope().serviceInstance, OperationKind::Control).outcome.code == ErrorCode::TransportUnavailable);
    CHECK(m.beginTransport(1).ok());
    const auto id = enqueue(m), second = enqueue(m);
    auto rev = m.snapshot()->registry.revision;
    CHECK(m.complete(id, OperationState::Succeeded, {}).code == ErrorCode::InvalidArgument);
    CHECK(m.complete(id, OperationState::Failed, {}).code == ErrorCode::InvalidArgument);
    CHECK(m.requireCleanup(id).code == ErrorCode::InvalidArgument);
    CHECK(m.recordCleanup(id, cleanupFailed).code == ErrorCode::InvalidArgument);
    CHECK(m.complete(id, OperationState::Running, failed).code == ErrorCode::InvalidArgument);
    CHECK(m.snapshot()->registry.revision == rev);
    CHECK(m.start(id).ok());
    CHECK(m.start(second).code == ErrorCode::Busy);
    CHECK(m.complete(id, OperationState::Succeeded, {}).ok());
    CHECK(m.complete(second, OperationState::Cancelled, {ErrorCode::OperationFailed, Reason::Cancelled}).ok());
    const auto timed = enqueue(m);
    CHECK(m.start(timed).ok());
    CHECK(m.complete(timed, OperationState::TimedOut, {ErrorCode::OperationFailed, Reason::Deadline}).ok());
    auto snap = m.snapshot();
    CHECK(snap->operations[1].state == OperationState::Cancelled);
    CHECK(snap->operations[2].state == OperationState::TimedOut);
    CHECK(snap->operations[1].primaryResult->reason != snap->operations[2].primaryResult->reason);
    CHECK(m.expire(id).ok()); CHECK(m.expire(second).ok()); CHECK(m.expire(timed).ok());
}
void staleQueuedAndBoundaries() {
    for (int boundary = 0; boundary < 7; ++boundary) {
        ServiceControlModel m([] { return 10; });
        CHECK(m.beginTransport(1).ok());
        const auto handle = bind(m);
        const auto id = enqueue(m, handle);
        const auto running = enqueue(m, handle);
        CHECK(m.start(running).ok()); CHECK(m.requireCleanup(running).ok());
        if (boundary == 0) CHECK(m.restartObservationSession().ok());
        if (boundary == 1) CHECK(m.beginTransport(2).ok());
        if (boundary == 2) CHECK(m.loseTransport().ok());
        if (boundary == 3) CHECK(bind(m, b) != handle);
        if (boundary == 4) CHECK(bind(m, a, 3) != handle);
        if (boundary == 5) CHECK(bind(m) != handle);
        if (boundary == 6) CHECK(m.quarantine(m.scope(), a, 0).code == ErrorCode::Ambiguous);
        CHECK(m.validate(m.scope().serviceInstance, handle).code == ErrorCode::StaleHandle);
        auto snap = m.snapshot();
        CHECK(snap->operations.front().state == OperationState::Failed);
        CHECK(snap->operations.back().state == OperationState::Failed);
        CHECK(snap->operations.back().cleanup == CleanupState::Unconfirmed);
        CHECK(m.start(id).code == ErrorCode::StaleHandle);
        CHECK(m.enqueue(m.scope().serviceInstance, OperationKind::Control, handle).outcome.code == ErrorCode::StaleHandle);
        CHECK(m.complete(running, OperationState::Succeeded, {}).code == ErrorCode::InvalidArgument);
        CHECK(!m.snapshot()->registry.devices.front().actionable);
    }
    ServiceControlModel old([] { return 10; }), fresh([] { return 10; });
    CHECK(old.beginTransport(1).ok()); CHECK(fresh.beginTransport(1).ok());
    const auto token = bind(old);
    const auto oldOperation = enqueue(old);
    const auto freshOperation = enqueue(fresh);
    CHECK(oldOperation != freshOperation);
    CHECK(fresh.start(oldOperation).code == ErrorCode::StaleHandle);
    CHECK(fresh.complete(oldOperation, OperationState::Cancelled, failed).code == ErrorCode::StaleHandle);
    CHECK(fresh.requireCleanup(oldOperation).code == ErrorCode::StaleHandle);
    CHECK(fresh.recordCleanup(oldOperation, {}).code == ErrorCode::StaleHandle);
    CHECK(fresh.expire(oldOperation).code == ErrorCode::StaleHandle);
    CHECK(fresh.snapshot()->operations.front().state == OperationState::Queued);
    CHECK(fresh.scope().serviceInstance != old.scope().serviceInstance);
    CHECK(fresh.enqueue(old.scope().serviceInstance, OperationKind::Control).outcome.code == ErrorCode::StaleHandle);
    CHECK(fresh.enqueue(fresh.scope().serviceInstance, OperationKind::Control, token).outcome.code == ErrorCode::StaleHandle);
}
void boundsAndOverflow() {
    ServiceControlModel m([] { return 10; }, {}, {2, 1});
    CHECK(m.beginTransport(1).ok());
    const auto first = enqueue(m);
    auto rev = m.snapshot()->registry.revision;
    CHECK(m.enqueue(m.scope().serviceInstance, OperationKind::Control).outcome.reason == Reason::QueueFull);
    CHECK(m.snapshot()->registry.revision == rev);
    CHECK(m.expire(first).code == ErrorCode::Busy);
    CHECK(m.start(first).ok());
    const auto second = enqueue(m);
    CHECK(m.enqueue(m.scope().serviceInstance, OperationKind::Control).outcome.reason == Reason::OperationRetentionFull);
    CHECK(m.snapshot()->operations.size() == 2);
    CHECK(m.complete(first, OperationState::Failed, failed).ok());
    CHECK(m.expire(first).ok());
    CHECK(m.start(second).ok());
    CHECK(enqueue(m) > second);

    ServiceControlModel counter([] { return 10; }, {}, {64, 32, 1});
    CHECK(counter.beginTransport(1).ok());
    const auto handle = bind(counter);
    const auto id = enqueue(counter);
    CHECK(counter.complete(id, OperationState::Cancelled, failed).ok()); CHECK(counter.expire(id).ok());
    CHECK(counter.enqueue(counter.scope().serviceInstance, OperationKind::Control).outcome.reason == Reason::CounterExhausted);
    CHECK(counter.snapshot()->registry.faulted && !counter.snapshot()->registry.devices.front().runtimeHandle);
    CHECK(!counter.validate(counter.scope().serviceInstance, handle).ok());

    ServiceControlModel attempts([] { return 10; }, {}, {64, 32, 1});
    CHECK(attempts.beginTransport(1).ok());
    const auto pending = enqueue(attempts);
    CHECK(attempts.start(pending).ok()); CHECK(attempts.requireCleanup(pending).ok());
    CHECK(attempts.complete(pending, OperationState::Failed, failed).ok());
    CHECK(attempts.recordCleanup(pending, cleanupFailed).ok());
    CHECK(attempts.recordCleanup(pending, {}).reason == Reason::CounterExhausted);
    CHECK(attempts.snapshot()->operations.front().primaryResult == failed);
    CHECK(attempts.snapshot()->operations.front().firstCleanupFailure == cleanupFailed);

    ServiceControlModel revision([] { return 10; }, {256, 5, 6});
    CHECK(revision.beginTransport(1).ok());
    const auto live = bind(revision);
    const auto active = enqueue(revision, live);
    CHECK(revision.start(active).ok()); CHECK(revision.requireCleanup(active).ok());
    CHECK(revision.metadata(revision.scope().serviceInstance, live).ok());
    CHECK(revision.metadata(revision.scope().serviceInstance, live).reason == Reason::CounterExhausted);
    const auto frozen = revision.snapshot();
    CHECK(frozen->registry.faulted && !frozen->registry.devices.front().runtimeHandle);
    CHECK(frozen->operations.front().state == OperationState::Failed);
    CHECK(frozen->operations.front().primaryResult->reason == Reason::CounterExhausted);
    CHECK(frozen->operations.front().cleanup == CleanupState::Unconfirmed);
    CHECK(frozen->registry.revision == 6); // never wrap or restart in the same namespace
}
void detachedAndTargetless() {
    std::shared_ptr<const ControlSnapshot> detached;
    {
        ServiceControlModel model([] { return 10; });
        CHECK(model.beginTransport(1).ok());
        (void)bind(model);
        (void)enqueue(model);
        detached = model.snapshot();
        CHECK(model.restartObservationSession().ok());
        CHECK(model.snapshot()->operations.front().state == OperationState::Failed);
    }
    CHECK(detached->registry.devices.front().actionable);
    CHECK(detached->operations.front().state == OperationState::Queued);
}
}
int main() try {
    static_assert(!std::is_copy_constructible_v<ServiceControlModel>);
    static_assert(std::is_same_v<decltype(std::declval<ServiceControlModel>().snapshot()), std::shared_ptr<const ControlSnapshot>>);
    primaryAndCleanup(); lifecycleAndQueue(); staleQueuedAndBoundaries(); boundsAndOverflow(); detachedAndTargetless();
    std::cout << "PASS operations, cleanup truth, immutable snapshots, queue and retention bounds, deferred stale targets and overflow\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
