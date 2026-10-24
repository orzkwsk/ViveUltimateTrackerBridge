#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace vut::control {

enum class ErrorCode {
    Ok, Unavailable, Busy, InvalidArgument, StaleHandle, TransportUnavailable,
    OperationFailed, CleanupUnconfirmed, Unsupported, Ambiguous,
    InternalConsistencyError, PermissionDenied
};
enum class Reason {
    None, ProviderNotInstalled, UnknownHandle, InstanceMismatch, RetiredBinding,
    SessionBoundary, TransportBoundary, Disconnected, Replaced, RepeatedPair,
    AmbiguousBinding, CounterExhausted, HistoryFull, OperationRetentionFull,
    QueueFull, InvalidTransition, InvalidTime, InvalidGeneration, InvalidSlot,
    IndexInconsistent, OperationExpired, CleanupPending, PrimaryFailure,
    CleanupFailure, Cancelled, Deadline, CapabilityUnavailable
};
struct Outcome {
    ErrorCode code = ErrorCode::Ok;
    Reason reason = Reason::None;
    [[nodiscard]] bool ok() const { return code == ErrorCode::Ok; }
    bool operator==(const Outcome&) const = default;
};
template<class T> struct Result {
    Outcome outcome;
    std::optional<T> value;
    [[nodiscard]] bool ok() const { return outcome.ok(); }
};

// Opaque values, not authorization credentials. No RF/slot-derived token or
// public constructor from bytes. Future IPC serialization needs its own review.
class ServiceInstance {
public:
    bool operator==(const ServiceInstance&) const = default;
    bool operator<(const ServiceInstance& rhs) const { return token_ < rhs.token_; }
private:
    std::array<std::uint32_t, 4> token_{};
    friend class DeviceRegistry;
};
class RuntimeHandle {
public:
    bool operator==(const RuntimeHandle&) const = default;
    bool operator<(const RuntimeHandle& rhs) const {
        if (instance_ != rhs.instance_) return instance_ < rhs.instance_;
        if (ordinal_ != rhs.ordinal_) return ordinal_ < rhs.ordinal_;
        return nonce_ < rhs.nonce_;
    }
private:
    ServiceInstance instance_;
    std::uint64_t ordinal_ = 0;
    std::array<std::uint32_t, 4> nonce_{};
    friend class DeviceRegistry;
};
using RfAddress = std::array<std::uint8_t, 6>;
class OperationId {
public:
    bool operator==(const OperationId&) const = default;
    bool operator<(const OperationId& rhs) const {
        if (instance_ != rhs.instance_) return instance_ < rhs.instance_;
        return ordinal_ < rhs.ordinal_;
    }
    bool operator>(const OperationId& rhs) const { return rhs < *this; }
private:
    ServiceInstance instance_;
    std::uint64_t ordinal_ = 0;
    friend class ServiceControlModel;
};
using MonotonicClock = std::function<std::int64_t()>;

struct Scope {
    ServiceInstance serviceInstance;
    std::uint64_t observationSession = 0, transportGeneration = 0;
    bool operator==(const Scope&) const = default;
};
struct Binding {
    Scope scope;
    std::uint64_t connectionEpoch = 0;
    RfAddress rf{};
    int slot = -1;
};
enum class ConnectionState { Observed, Connected, Disconnected, BindingAmbiguous };
enum class TrackingState { Unknown, Initializing, Tracking, Lost };
enum class PoseFreshness { NeverObserved, Fresh, Stale, Unknown };
enum class Admission { Unknown, Admitted, Unconfigured, Rejected };
enum class IdentityState { Unavailable, Candidate, Verified, Ambiguous, Stale };
struct FreshnessPolicy {
    // Absent threshold means Unknown. This is software policy, not vendor liveness.
    std::optional<std::int64_t> maximumAgeNs;
    std::uint64_t policyId = 0;
};
struct RegistryLimits {
    std::size_t maximumRecords = 256; // current + historical; no silent eviction
    std::size_t maximumCurrent = 5;
    // A smaller ceiling permits deterministic exhaustion tests, never wrapping.
    std::uint64_t counterCeiling = std::numeric_limits<std::uint64_t>::max();
};
struct DeviceSnapshot {
    std::uint64_t recordId = 0;
    Binding binding;
    std::optional<RuntimeHandle> runtimeHandle;
    bool actionable = false;
    ConnectionState connection = ConnectionState::Disconnected;
    TrackingState tracking = TrackingState::Unknown;
    std::uint8_t trackingRaw = 0xff;
    Admission admission = Admission::Unknown;
    std::optional<std::int64_t> lastSeenAgeNs, lastPoseReceiveAgeNs, lastAcceptedPoseAgeNs;
    PoseFreshness freshness = PoseFreshness::NeverObserved;
    std::uint64_t freshnessPolicyId = 0;
    IdentityState identity = IdentityState::Unavailable;
    Reason identityReason = Reason::ProviderNotInstalled;
    std::vector<OperationId> operationIds;
    Reason warning = Reason::None;
};
struct RegistrySnapshot {
    Scope scope;
    std::uint64_t revision = 0;
    std::int64_t capturedAtNs = 0;
    bool transportAvailable = false, faulted = false;
    Reason warning = Reason::None;
    std::vector<DeviceSnapshot> devices;
};

class ServiceControlModel;
struct DeviceRegistryTestAccess;
// Single-writer value model. Call only on the future owner executor. Readers
// receive detached const snapshots; neither Backend nor mutable records escape.
class DeviceRegistry {
public:
    explicit DeviceRegistry(MonotonicClock clock, RegistryLimits limits = {});
    DeviceRegistry(const DeviceRegistry&) = delete;
    DeviceRegistry& operator=(const DeviceRegistry&) = delete;
    DeviceRegistry(DeviceRegistry&&) = delete;
    DeviceRegistry& operator=(DeviceRegistry&&) = delete;
    [[nodiscard]] Scope scope() const { return scope_; }
    Outcome beginTransport(std::uint64_t generation);
    Outcome loseTransport();
    Outcome restartObservationSession();
    Result<RuntimeHandle> bind(const Scope&, RfAddress, int slot,
                              ConnectionState = ConnectionState::Connected);
    Outcome disconnect(const ServiceInstance&, const RuntimeHandle&);
    Outcome quarantine(const Scope&, RfAddress, int slot);
    Outcome metadata(const ServiceInstance&, const RuntimeHandle&);
    // Caller supplies existing decoder state and actual adapter admission; this
    // model neither decodes vendor status nor invents pose validity/capabilities.
    Outcome pose(const ServiceInstance&, const RuntimeHandle&, TrackingState,
                 std::uint8_t raw, Admission);
    [[nodiscard]] Outcome validate(const ServiceInstance&, const RuntimeHandle&);
    [[nodiscard]] std::shared_ptr<const RegistrySnapshot> snapshot(FreshnessPolicy = {}) const;
private:
    struct Record {
        std::uint64_t id;
        Binding binding;
        std::optional<RuntimeHandle> handle;
        ConnectionState connection;
        TrackingState tracking = TrackingState::Unknown;
        std::uint8_t trackingRaw = 0xff;
        Admission admission = Admission::Unknown;
        std::optional<std::int64_t> seen, received, accepted;
        IdentityState identity = IdentityState::Unavailable;
        Reason warning = Reason::None;
        std::vector<OperationId> operations;
    };
    MonotonicClock clock_;
    RegistryLimits limits_;
    Scope scope_;
    std::uint64_t revision_ = 0, nextEpoch_ = 0;
    std::int64_t lastMutationNs_ = 0;
    bool transport_ = false, faulted_ = false;
    Reason warning_ = Reason::None;
    std::vector<Record> records_;
    std::map<int, std::uint64_t> slots_;
    std::map<RfAddress, std::uint64_t> radios_;
    std::map<RuntimeHandle, std::uint64_t> handles_;
    Record* find(std::uint64_t);
    bool consistent() const;
    Outcome prepare(std::int64_t& now);
    Outcome checkScope(const Scope&) const;
    Outcome trip(Reason);
    void retire(Record&, Reason, bool ambiguous = false);
    void retireAll(Reason);
    void changed(std::int64_t now);
    friend class ServiceControlModel;
    friend struct DeviceRegistryTestAccess; // private invariant-fault injection in tests
};

} // namespace vut::control
