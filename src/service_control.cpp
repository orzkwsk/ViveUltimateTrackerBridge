#include "vut/service_control.hpp"

#include <algorithm>
#include <stdexcept>

namespace vut::control {
namespace {
bool terminal(OperationState state) {
    return state == OperationState::Succeeded || state == OperationState::Failed ||
        state == OperationState::Cancelled || state == OperationState::TimedOut;
}
bool cleanupBlocks(const OperationRecord& op) {
    return op.cleanup == CleanupState::Pending || op.cleanup == CleanupState::Unconfirmed;
}
}
ServiceControlModel::ServiceControlModel(MonotonicClock clock, RegistryLimits registryLimits,
                                         OperationLimits limits)
    : registry_(std::move(clock), registryLimits), limits_(limits) {
    if (!limits.maximumRetained || !limits.maximumQueued || !limits.counterCeiling)
        throw std::invalid_argument("invalid pure operation configuration");
}
OperationRecord* ServiceControlModel::find(OperationId id) {
    const auto it = std::find_if(operations_.begin(), operations_.end(),
        [id](const auto& op) { return op.id == id; });
    return it == operations_.end() ? nullptr : &*it;
}
void ServiceControlModel::reconcile(Reason why) {
    // Pure invalidation only. No cancellation, disable or vendor command sent.
    for (auto& op : operations_) if (!terminal(op.state)) {
        const bool stale = registry_.faulted_ || !registry_.transport_ || op.scope != scope() ||
            (op.targetHandle && !registry_.validate(scope().serviceInstance, *op.targetHandle).ok());
        if (!stale) continue;
        op.state = OperationState::Failed;
        op.primaryResult = Outcome{ErrorCode::OperationFailed, why};
        op.updatedAtNs = registry_.lastMutationNs_;
        if (op.cleanup == CleanupState::Pending) {
            op.cleanup = CleanupState::Unconfirmed;
            op.cleanupResult = Outcome{ErrorCode::CleanupUnconfirmed, why};
            if (!op.firstCleanupFailure)
                op.firstCleanupFailure = op.cleanupResult;
        }
    }
}
Outcome ServiceControlModel::beginTransport(std::uint64_t generation) {
    const auto out = registry_.beginTransport(generation); reconcile(out.ok() ? Reason::TransportBoundary : out.reason); return out;
}
Outcome ServiceControlModel::loseTransport() {
    const auto out = registry_.loseTransport(); reconcile(out.ok() ? Reason::TransportBoundary : out.reason); return out;
}
Outcome ServiceControlModel::restartObservationSession() {
    const auto out = registry_.restartObservationSession(); reconcile(out.ok() ? Reason::SessionBoundary : out.reason); return out;
}
Result<RuntimeHandle> ServiceControlModel::bind(const Scope& expected, RfAddress rf, int slot, ConnectionState state) {
    const auto out = registry_.bind(expected, rf, slot, state); reconcile(out.ok() ? Reason::RetiredBinding : out.outcome.reason); return out;
}
Outcome ServiceControlModel::disconnect(const ServiceInstance& instance, const RuntimeHandle& handle) {
    const auto out = registry_.disconnect(instance, handle); reconcile(out.ok() ? Reason::Disconnected : out.reason); return out;
}
Outcome ServiceControlModel::quarantine(const Scope& expected, RfAddress rf, int slot) {
    const auto out = registry_.quarantine(expected, rf, slot); reconcile(out.ok() ? Reason::AmbiguousBinding : out.reason); return out;
}
Outcome ServiceControlModel::metadata(const ServiceInstance& instance, const RuntimeHandle& handle) {
    const auto out = registry_.metadata(instance, handle); reconcile(out.ok() ? Reason::RetiredBinding : out.reason); return out;
}
Outcome ServiceControlModel::pose(const ServiceInstance& instance, const RuntimeHandle& handle,
                                 TrackingState state, std::uint8_t raw, Admission admission) {
    const auto out = registry_.pose(instance, handle, state, raw, admission); reconcile(out.ok() ? Reason::RetiredBinding : out.reason); return out;
}
Outcome ServiceControlModel::validate(const ServiceInstance& instance, const RuntimeHandle& handle) {
    const auto out = registry_.validate(instance, handle); reconcile(out.ok() ? Reason::RetiredBinding : out.reason); return out;
}
Result<OperationId> ServiceControlModel::enqueue(const ServiceInstance& expected, OperationKind kind,
                                                std::optional<RuntimeHandle> target) {
    if (kind < OperationKind::Pairing || kind > OperationKind::Control)
        return {{ErrorCode::InvalidArgument, Reason::InvalidTransition}, {}};
    if (expected != scope().serviceInstance) return {{ErrorCode::StaleHandle, Reason::InstanceMismatch}, {}};
    if (target) if (const auto e = validate(expected, *target); !e.ok()) return {e, {}};
    std::int64_t now;
    if (const auto e = registry_.prepare(now); !e.ok()) { reconcile(e.reason); return {e, {}}; }
    if (std::any_of(operations_.begin(), operations_.end(), cleanupBlocks))
        return {{ErrorCode::CleanupUnconfirmed, Reason::CleanupPending}, {}};
    if (!registry_.transport_) return {{ErrorCode::TransportUnavailable, Reason::TransportBoundary}, {}};
    if (operations_.size() >= limits_.maximumRetained)
        return {{ErrorCode::Busy, Reason::OperationRetentionFull}, {}};
    if (std::count_if(operations_.begin(), operations_.end(), [](const auto& op) {
        return op.state == OperationState::Queued; }) >= static_cast<std::ptrdiff_t>(limits_.maximumQueued))
        return {{ErrorCode::Busy, Reason::QueueFull}, {}};
    if (nextOperation_ >= limits_.counterCeiling) {
        const auto e = registry_.trip(Reason::CounterExhausted); reconcile(e.reason); return {e, {}};
    }
    OperationRecord op;
    op.id.instance_ = scope().serviceInstance;
    op.id.ordinal_ = ++nextOperation_; op.kind = kind; op.scope = scope();
    op.createdAtNs = op.updatedAtNs = now; op.targetHandle = target;
    operations_.push_back(op);
    if (target) registry_.find(registry_.handles_.at(*target))->operations.push_back(op.id);
    registry_.changed(now);
    return {{}, op.id};
}
Outcome ServiceControlModel::start(OperationId id) {
    if (id.instance_ != scope().serviceInstance) return {ErrorCode::StaleHandle, Reason::InstanceMismatch};
    auto* op = find(id);
    if (!op) return {ErrorCode::Unavailable, Reason::OperationExpired};
    if (op->scope != scope()) return {ErrorCode::StaleHandle, Reason::SessionBoundary};
    if (op->targetHandle) if (const auto e = validate(scope().serviceInstance, *op->targetHandle); !e.ok()) return e;
    if (op->state != OperationState::Queued) return {ErrorCode::InvalidArgument, Reason::InvalidTransition};
    std::int64_t now;
    if (const auto e = registry_.prepare(now); !e.ok()) { reconcile(e.reason); return e; }
    if (std::any_of(operations_.begin(), operations_.end(), cleanupBlocks))
        return {ErrorCode::CleanupUnconfirmed, Reason::CleanupPending};
    if (!registry_.transport_) return {ErrorCode::TransportUnavailable, Reason::TransportBoundary};
    if (std::any_of(operations_.begin(), operations_.end(), [](const auto& other) { return other.state == OperationState::Running; }))
        return {ErrorCode::Busy, Reason::QueueFull};
    op->state = OperationState::Running; op->updatedAtNs = now;
    registry_.changed(now);
    return {};
}
Outcome ServiceControlModel::complete(OperationId id, OperationState state, Outcome primary) {
    if (id.instance_ != scope().serviceInstance) return {ErrorCode::StaleHandle, Reason::InstanceMismatch};
    auto* op = find(id);
    if (!op) return {ErrorCode::Unavailable, Reason::OperationExpired};
    if (!terminal(state) || terminal(op->state) ||
        (state == OperationState::Succeeded && (op->state != OperationState::Running || !primary.ok())) ||
        (state != OperationState::Succeeded && primary.ok()))
        return {ErrorCode::InvalidArgument, Reason::InvalidTransition};
    std::int64_t now;
    if (const auto e = registry_.prepare(now); !e.ok()) { reconcile(e.reason); return e; }
    op->state = state; op->primaryResult = primary; op->updatedAtNs = now;
    registry_.changed(now);
    return {};
}
Outcome ServiceControlModel::requireCleanup(OperationId id) {
    if (id.instance_ != scope().serviceInstance) return {ErrorCode::StaleHandle, Reason::InstanceMismatch};
    auto* op = find(id);
    if (!op) return {ErrorCode::Unavailable, Reason::OperationExpired};
    if (op->state != OperationState::Running || op->cleanup != CleanupState::NotNeeded)
        return {ErrorCode::InvalidArgument, Reason::InvalidTransition};
    std::int64_t now;
    if (const auto e = registry_.prepare(now); !e.ok()) { reconcile(e.reason); return e; }
    op->cleanup = CleanupState::Pending; op->updatedAtNs = now;
    registry_.changed(now);
    return {};
}
Outcome ServiceControlModel::recordCleanup(OperationId id, Outcome result) {
    if (id.instance_ != scope().serviceInstance) return {ErrorCode::StaleHandle, Reason::InstanceMismatch};
    auto* op = find(id);
    if (!op) return {ErrorCode::Unavailable, Reason::OperationExpired};
    if ((op->cleanup != CleanupState::Pending && op->cleanup != CleanupState::Unconfirmed) ||
        (!result.ok() && result.code != ErrorCode::CleanupUnconfirmed))
        return {ErrorCode::InvalidArgument, Reason::InvalidTransition};
    std::int64_t now;
    if (const auto e = registry_.prepare(now); !e.ok()) { reconcile(e.reason); return e; }
    if (op->cleanupAttempts >= limits_.counterCeiling) {
        const auto e = registry_.trip(Reason::CounterExhausted); reconcile(e.reason); return e;
    }
    ++op->cleanupAttempts;
    op->cleanup = result.ok() ? CleanupState::Confirmed : CleanupState::Unconfirmed;
    op->cleanupResult = result;
    if (!result.ok() && !op->firstCleanupFailure) op->firstCleanupFailure = result;
    op->updatedAtNs = now;
    registry_.changed(now);
    return {};
}
Outcome ServiceControlModel::expire(OperationId id) {
    if (id.instance_ != scope().serviceInstance) return {ErrorCode::StaleHandle, Reason::InstanceMismatch};
    const auto* op = find(id);
    if (!op) return {ErrorCode::Unavailable, Reason::OperationExpired};
    if (!terminal(op->state) || cleanupBlocks(*op)) return {ErrorCode::Busy, Reason::CleanupPending};
    std::int64_t now;
    if (const auto e = registry_.prepare(now); !e.ok()) { reconcile(e.reason); return e; }
    std::erase_if(operations_, [id](const auto& r) { return r.id == id; });
    for (auto& r : registry_.records_) std::erase(r.operations, id);
    registry_.changed(now);
    return {};
}
std::shared_ptr<const ControlSnapshot> ServiceControlModel::snapshot(FreshnessPolicy policy) const {
    auto out = std::make_shared<ControlSnapshot>();
    out->registry = *registry_.snapshot(policy); out->operations = operations_;
    return out;
}
} // namespace vut::control
