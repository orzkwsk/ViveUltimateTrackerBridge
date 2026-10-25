#include "vut/device_registry.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>

namespace vut::control {
namespace {
std::array<std::uint32_t, 4> randomToken() {
    std::random_device source;
    std::uniform_int_distribution<std::uint32_t> distribution;
    std::array<std::uint32_t, 4> token;
    do { for (auto& word : token) word = distribution(source); }
    while (token == std::array<std::uint32_t, 4>{});
    return token;
}
std::optional<std::int64_t> age(std::optional<std::int64_t> sample, std::int64_t now) {
    if (!sample || now < *sample || now < 0) return {};
    return now - *sample;
}
}

DeviceRegistry::DeviceRegistry(MonotonicClock clock, RegistryLimits limits)
    : clock_(std::move(clock)), limits_(limits) {
    if (!clock_ || limits.maximumRecords == 0 || limits.maximumCurrent == 0 ||
        limits.maximumCurrent > 5 || limits.counterCeiling == 0)
        throw std::invalid_argument("invalid pure registry configuration");
    scope_.serviceInstance.token_ = randomToken();
    scope_.observationSession = 1;
}
DeviceRegistry::Record* DeviceRegistry::find(std::uint64_t id) {
    const auto it = std::find_if(records_.begin(), records_.end(),
        [id](const auto& record) { return record.id == id; });
    return it == records_.end() ? nullptr : &*it;
}
bool DeviceRegistry::consistent() const {
    std::size_t live = 0;
    // Records are append-only, in strictly increasing epoch order. Check that
    // invariant without allocating a temporary tree on each receive/admission.
    std::uint64_t previousId = 0;
    for (const auto& r : records_) {
        if (r.id <= previousId) return false;
        previousId = r.id;
        if (!r.handle) continue;
        ++live;
        if (!transport_ || faulted_ || r.binding.scope != scope_ ||
            r.binding.connectionEpoch != r.id || r.handle->instance_ != scope_.serviceInstance ||
            r.handle->ordinal_ != r.id ||
            (r.connection != ConnectionState::Connected && r.connection != ConnectionState::Observed))
            return false;
        const auto s = slots_.find(r.binding.slot);
        const auto rf = radios_.find(r.binding.rf);
        const auto h = handles_.find(*r.handle);
        if (s == slots_.end() || s->second != r.id || rf == radios_.end() ||
            rf->second != r.id || h == handles_.end() || h->second != r.id) return false;
    }
    return live <= limits_.maximumCurrent && records_.size() <= limits_.maximumRecords &&
        live == slots_.size() && live == radios_.size() && live == handles_.size();
}
Outcome DeviceRegistry::trip(Reason reason) {
    // Explicit safety quarantine, never an attempted repair/rebinding. The
    // exhausted revision is retained instead of wrapping; faulted status wins.
    for (auto& r : records_) {
        r.handle.reset();
        r.connection = ConnectionState::BindingAmbiguous;
        r.identity = IdentityState::Stale;
        r.warning = reason;
    }
    handles_.clear(); slots_.clear(); radios_.clear();
    faulted_ = true;
    transport_ = false;
    warning_ = reason;
    if (revision_ < limits_.counterCeiling) ++revision_;
    return {reason == Reason::HistoryFull ? ErrorCode::Busy : ErrorCode::InternalConsistencyError, reason};
}
Outcome DeviceRegistry::prepare(std::int64_t& now) {
    if (faulted_) return {ErrorCode::InternalConsistencyError, warning_};
    if (!consistent()) return trip(Reason::IndexInconsistent);
    if (revision_ >= limits_.counterCeiling) return trip(Reason::CounterExhausted);
    try { now = clock_(); }
    catch (...) { return trip(Reason::InvalidTime); }
    if (now < lastMutationNs_ || now < 0) return trip(Reason::InvalidTime);
    return {};
}
void DeviceRegistry::changed(std::int64_t now) { ++revision_; lastMutationNs_ = now; }
Outcome DeviceRegistry::checkScope(const Scope& expected) const {
    if (expected.serviceInstance != scope_.serviceInstance)
        return {ErrorCode::StaleHandle, Reason::InstanceMismatch};
    if (expected != scope_) return {ErrorCode::StaleHandle, Reason::SessionBoundary};
    return {};
}
void DeviceRegistry::retire(Record& r, Reason why, bool ambiguous) {
    if (r.handle) {
        handles_.erase(*r.handle); slots_.erase(r.binding.slot); radios_.erase(r.binding.rf);
    }
    r.handle.reset();
    r.connection = ambiguous ? ConnectionState::BindingAmbiguous : ConnectionState::Disconnected;
    r.identity = ambiguous ? IdentityState::Ambiguous : IdentityState::Stale;
    r.warning = why;
}
void DeviceRegistry::retireAll(Reason why) {
    for (auto& r : records_) if (r.handle) retire(r, why);
}
Outcome DeviceRegistry::beginTransport(std::uint64_t generation) {
    if (generation == 0 || generation <= scope_.transportGeneration)
        return {ErrorCode::InvalidArgument, Reason::InvalidGeneration};
    if (generation > limits_.counterCeiling) return trip(Reason::CounterExhausted);
    std::int64_t now;
    if (const auto error = prepare(now); !error.ok()) return error;
    if (scope_.observationSession >= limits_.counterCeiling) return trip(Reason::CounterExhausted);
    retireAll(Reason::TransportBoundary);
    ++scope_.observationSession;
    scope_.transportGeneration = generation;
    transport_ = true;
    changed(now);
    return {};
}
Outcome DeviceRegistry::loseTransport() {
    std::int64_t now;
    if (const auto error = prepare(now); !error.ok()) return error;
    retireAll(Reason::TransportBoundary);
    transport_ = false;
    changed(now);
    return {};
}
Outcome DeviceRegistry::restartObservationSession() {
    std::int64_t now;
    if (const auto error = prepare(now); !error.ok()) return error;
    if (scope_.observationSession >= limits_.counterCeiling) return trip(Reason::CounterExhausted);
    retireAll(Reason::SessionBoundary);
    ++scope_.observationSession;
    changed(now);
    return {};
}
Result<RuntimeHandle> DeviceRegistry::bind(const Scope& expected, RfAddress rf, int slot,
                                          ConnectionState connection) {
    if (slot < 0 || slot >= 5) return {{ErrorCode::InvalidArgument, Reason::InvalidSlot}, {}};
    if (connection != ConnectionState::Connected && connection != ConnectionState::Observed)
        return {{ErrorCode::InvalidArgument, Reason::InvalidTransition}, {}};
    if (const auto error = checkScope(expected); !error.ok()) return {error, {}};
    std::int64_t now;
    if (const auto error = prepare(now); !error.ok()) return {error, {}};
    if (!transport_) return {{ErrorCode::TransportUnavailable, Reason::TransportBoundary}, {}};
    const auto s = slots_.find(slot);
    const auto radio = radios_.find(rf);
    const auto sid = s == slots_.end() ? 0 : s->second;
    const auto rid = radio == radios_.end() ? 0 : radio->second;
    if (connection == ConnectionState::Observed && sid && sid == rid)
        return {{}, find(sid)->handle}; // no new pair boundary
    if ((sid && rid && sid != rid) || (connection == ConnectionState::Observed && (sid || rid))) {
        const auto error = quarantine(expected, rf, slot);
        return {error, {}};
    }
    if (records_.size() >= limits_.maximumRecords) return {trip(Reason::HistoryFull), {}};
    if (nextEpoch_ >= limits_.counterCeiling) return {trip(Reason::CounterExhausted), {}};
    if (!sid && !rid && handles_.size() >= limits_.maximumCurrent)
        return {{ErrorCode::Busy, Reason::HistoryFull}, {}};
    RuntimeHandle handle;
    try { handle.nonce_ = randomToken(); }
    catch (...) { return {trip(Reason::IndexInconsistent), {}}; }
    handle.instance_ = scope_.serviceInstance;
    handle.ordinal_ = nextEpoch_ + 1;
    if (sid) retire(*find(sid), sid == rid ? Reason::RepeatedPair : Reason::Replaced);
    if (rid && rid != sid) retire(*find(rid), Reason::Replaced);
    ++nextEpoch_;
    Record record{nextEpoch_, {scope_, nextEpoch_, rf, slot}, handle, connection};
    record.seen = now;
    records_.push_back(std::move(record));
    slots_.emplace(slot, nextEpoch_);
    radios_.emplace(rf, nextEpoch_);
    handles_.emplace(handle, nextEpoch_);
    changed(now);
    return {{}, handle};
}
Outcome DeviceRegistry::validate(const ServiceInstance& instance, const RuntimeHandle& handle) {
    if (instance != scope_.serviceInstance || handle.instance_ != scope_.serviceInstance)
        return {ErrorCode::StaleHandle, Reason::InstanceMismatch};
    if (faulted_) return {ErrorCode::InternalConsistencyError, warning_};
    if (!consistent()) return trip(Reason::IndexInconsistent);
    if (handles_.find(handle) == handles_.end()) return {ErrorCode::StaleHandle, Reason::UnknownHandle};
    if (!transport_) return {ErrorCode::TransportUnavailable, Reason::TransportBoundary};
    return {};
}
Outcome DeviceRegistry::disconnect(const ServiceInstance& instance, const RuntimeHandle& handle) {
    if (const auto error = validate(instance, handle); !error.ok()) return error;
    std::int64_t now;
    if (const auto error = prepare(now); !error.ok()) return error;
    retire(*find(handles_.at(handle)), Reason::Disconnected);
    changed(now);
    return {};
}
Outcome DeviceRegistry::quarantine(const Scope& expected, RfAddress rf, int slot) {
    if (slot < 0 || slot >= 5) return {ErrorCode::InvalidArgument, Reason::InvalidSlot};
    if (const auto error = checkScope(expected); !error.ok()) return error;
    std::int64_t now;
    if (const auto error = prepare(now); !error.ok()) return error;
    for (auto& r : records_) if (r.handle && (r.binding.rf == rf || r.binding.slot == slot))
        retire(r, Reason::AmbiguousBinding, true);
    changed(now);
    return {ErrorCode::Ambiguous, Reason::AmbiguousBinding};
}
Outcome DeviceRegistry::metadata(const ServiceInstance& instance, const RuntimeHandle& handle) {
    if (const auto error = validate(instance, handle); !error.ok()) return error;
    std::int64_t now;
    if (const auto error = prepare(now); !error.ok()) return error;
    find(handles_.at(handle))->seen = now;
    changed(now);
    return {};
}
Outcome DeviceRegistry::pose(const ServiceInstance& instance, const RuntimeHandle& handle,
                             TrackingState tracking, std::uint8_t raw, Admission admission) {
    if (tracking < TrackingState::Unknown || tracking > TrackingState::Lost ||
        admission < Admission::Unknown || admission > Admission::Rejected)
        return {ErrorCode::InvalidArgument, Reason::InvalidTransition};
    if (const auto error = validate(instance, handle); !error.ok()) return error;
    std::int64_t now;
    if (const auto error = prepare(now); !error.ok()) return error;
    auto& r = *find(handles_.at(handle));
    r.seen = now; r.received = now;
    if (admission == Admission::Admitted) r.accepted = now;
    r.tracking = tracking; r.trackingRaw = raw; r.admission = admission;
    changed(now);
    return {};
}
std::shared_ptr<const RegistrySnapshot> DeviceRegistry::snapshot(FreshnessPolicy policy) const {
    auto out = std::make_shared<RegistrySnapshot>();
    out->scope = scope_; out->revision = revision_; out->transportAvailable = transport_;
    out->faulted = faulted_ || !consistent(); out->warning = warning_;
    if (!consistent()) out->warning = Reason::IndexInconsistent;
    bool validTime = true;
    try { out->capturedAtNs = clock_(); }
    catch (...) { validTime = false; }
    validTime = validTime && out->capturedAtNs >= lastMutationNs_ && out->capturedAtNs >= 0;
    if (!validTime) { out->warning = Reason::InvalidTime; out->faulted = true; }
    for (const auto& r : records_) {
        DeviceSnapshot d;
        d.recordId = r.id; d.binding = r.binding;
        d.actionable = r.handle.has_value() && transport_ && !out->faulted;
        if (d.actionable) d.runtimeHandle = r.handle;
        d.connection = r.connection; d.tracking = r.tracking; d.trackingRaw = r.trackingRaw;
        d.admission = r.admission; d.identity = r.identity;
        d.identityReason = r.identity == IdentityState::Unavailable ? Reason::ProviderNotInstalled : r.warning;
        d.warning = r.warning; d.operationIds = r.operations;
        if (validTime) {
            d.lastSeenAgeNs = age(r.seen, out->capturedAtNs);
            d.lastPoseReceiveAgeNs = age(r.received, out->capturedAtNs);
            d.lastAcceptedPoseAgeNs = age(r.accepted, out->capturedAtNs);
        }
        d.freshnessPolicyId = policy.policyId;
        if (!r.accepted) d.freshness = PoseFreshness::NeverObserved;
        else if (!d.actionable || !d.lastAcceptedPoseAgeNs || !policy.maximumAgeNs || *policy.maximumAgeNs < 0)
            d.freshness = PoseFreshness::Unknown;
        else d.freshness = *d.lastAcceptedPoseAgeNs <= *policy.maximumAgeNs ? PoseFreshness::Fresh : PoseFreshness::Stale;
        out->devices.push_back(std::move(d));
    }
    return out;
}
} // namespace vut::control
