#pragma once

#include "vut/device_registry.hpp"

namespace vut::control {

enum class OperationKind { Pairing, Identify, Control };
enum class OperationState { Queued, Running, Succeeded, Failed, Cancelled, TimedOut };
enum class CleanupState { NotNeeded, Pending, Confirmed, Unconfirmed };
struct OperationRecord {
    OperationId id;
    OperationKind kind = OperationKind::Control;
    OperationState state = OperationState::Queued;
    Scope scope;
    std::optional<Outcome> primaryResult;
    CleanupState cleanup = CleanupState::NotNeeded;
    std::optional<Outcome> cleanupResult;
    std::optional<Outcome> firstCleanupFailure;
    std::uint64_t cleanupAttempts = 0;
    std::int64_t createdAtNs = 0, updatedAtNs = 0;
    std::optional<RuntimeHandle> targetHandle;
};
struct OperationLimits {
    std::size_t maximumRetained = 64, maximumQueued = 32;
    std::uint64_t counterCeiling = std::numeric_limits<std::uint64_t>::max();
};
struct ControlSnapshot {
    RegistrySnapshot registry;
    std::vector<OperationRecord> operations;
};

class ServiceControlModel {
public:
    explicit ServiceControlModel(MonotonicClock, RegistryLimits = {}, OperationLimits = {});
    ServiceControlModel(const ServiceControlModel&) = delete;
    ServiceControlModel& operator=(const ServiceControlModel&) = delete;
    [[nodiscard]] Scope scope() const { return registry_.scope(); }
    Outcome beginTransport(std::uint64_t generation);
    Outcome loseTransport();
    Outcome restartObservationSession();
    Result<RuntimeHandle> bind(const Scope&, RfAddress, int slot,
                              ConnectionState = ConnectionState::Connected);
    Outcome disconnect(const ServiceInstance&, const RuntimeHandle&);
    Outcome quarantine(const Scope&, RfAddress, int slot);
    Outcome metadata(const ServiceInstance&, const RuntimeHandle&);
    Outcome pose(const ServiceInstance&, const RuntimeHandle&, TrackingState, std::uint8_t, Admission);
    Outcome validate(const ServiceInstance&, const RuntimeHandle&);
    Result<OperationId> enqueue(const ServiceInstance&, OperationKind,
                                std::optional<RuntimeHandle> target = {});
    Outcome start(OperationId);
    Outcome complete(OperationId, OperationState terminal, Outcome primary);
    Outcome requireCleanup(OperationId);
    Outcome recordCleanup(OperationId, Outcome);
    // Explicit retention release: never evict active or uncertain-cleanup work.
    Outcome expire(OperationId);
    [[nodiscard]] std::shared_ptr<const ControlSnapshot> snapshot(FreshnessPolicy = {}) const;
private:
    DeviceRegistry registry_;
    OperationLimits limits_;
    std::uint64_t nextOperation_ = 0;
    std::vector<OperationRecord> operations_;
    OperationRecord* find(OperationId);
    void reconcile(Reason);
};

} // namespace vut::control
