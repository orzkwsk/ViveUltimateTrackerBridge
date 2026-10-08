#include "vut/setup.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vut::setup {
StableIdentity stableIdentity(IdentityKind kind, std::string id) {
    if (id.empty() || id.size() > 256 || std::any_of(id.begin(), id.end(),
        [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
        throw std::invalid_argument("invalid stable identity identifier");
    switch (kind) {
    case IdentityKind::HardwareSerial: case IdentityKind::StableUuid:
    case IdentityKind::PersistentIdentifier: break;
    default: throw std::invalid_argument("invalid identity kind");
    }
    return {kind, std::move(id)};
}
std::string StableIdentity::key() const {
    (void)stableIdentity(kind, identifier);
    switch (kind) {
    case IdentityKind::HardwareSerial: return "serial:" + identifier;
    case IdentityKind::StableUuid: return "uuid:" + identifier;
    case IdentityKind::PersistentIdentifier: return "persistent:" + identifier;
    }
    throw std::invalid_argument("invalid identity kind");
}
StableIdentity parseStableIdentity(std::string_view key) {
    for (const auto& [prefix, kind] : std::vector<std::pair<std::string_view, IdentityKind>>{
        {"serial:", IdentityKind::HardwareSerial}, {"uuid:", IdentityKind::StableUuid},
        {"persistent:", IdentityKind::PersistentIdentifier}})
        if (key.starts_with(prefix)) return stableIdentity(kind, std::string(key.substr(prefix.size())));
    throw std::invalid_argument("stable identity must have serial:, uuid: or persistent: provenance");
}
void IdentifyPolicy::validate() const {
    if (minimumSamples < 2 || minimumSamples > 1000000 || minimumMovements == 0 ||
        minimumMovements > 1000000 || minimumSpanNs <= 0 || freshnessNs <= 0 ||
        windowNs < minimumSpanNs || windowNs > 3600000000000LL || freshnessNs > windowNs)
        throw std::invalid_argument("invalid Identify sample/time policy");
    for (double v : {positionThresholdMeters, angularThresholdRadians, positionNoiseMeters,
                     angularNoiseRadians, positionJumpMeters, angularJumpRadians})
        if (!std::isfinite(v) || v <= 0) throw std::invalid_argument("invalid Identify motion policy");
    if (positionNoiseMeters > positionJumpMeters || angularNoiseRadians > angularJumpRadians)
        throw std::invalid_argument("Identify noise threshold exceeds jump threshold");
}
Devices::Devices(IdentifyPolicy policy) : policy_(policy) { policy_.validate(); }
bool MotionEvidence::candidate(std::int64_t now, const IdentifyPolicy& policy) const {
    return samples >= policy.minimumSamples && movements >= policy.minimumMovements &&
        lastNs - firstNs >= policy.minimumSpanNs && now >= lastNs && now - lastNs <= policy.freshnessNs &&
        (positionDeltaMeters >= policy.positionThresholdMeters || angularDeltaRadians >= policy.angularThresholdRadians);
}
void Devices::endSession() {
    for (auto& [_, device] : devices_) {
        device.connected = false; device.identity.reset(); device.previous.reset(); device.motion = {};
        device.tracking = TrackingStatus::Unknown; device.lastPoseNs = 0;
        device.warning = "Stable hardware identity unavailable; persistent assignment blocked";
    }
    slots_.clear(); identifyStart_.reset();
}
void Devices::beginSession(std::uint64_t generation) { endSession(); generation_ = generation; }
void Devices::observeIdentity(const std::string& rf, const StableIdentity& identity) {
    (void)identity.key();
    auto it = devices_.find(rf);
    if (it == devices_.end() || !it->second.connected)
        throw std::invalid_argument("identity evidence requires a connected device");
    for (const auto& [otherRf, device] : devices_)
        if (otherRf != rf && device.connected && device.identity && device.identity->key() == identity.key())
            throw std::invalid_argument("duplicate live stable identity; assignment blocked");
    if (it->second.identity && it->second.identity->key() != identity.key())
        throw std::invalid_argument("identity changed within connected device; reconnect required");
    it->second.identity = identity; it->second.warning.clear();
}
void Devices::beginIdentify(std::int64_t now) {
    identifyStart_ = now;
    for (auto& [_, d] : devices_) { d.motion = {}; d.previous.reset(); }
}
bool Devices::identifying(std::int64_t now) const {
    return identifyStart_ && now >= *identifyStart_ && now - *identifyStart_ <= policy_.windowNs;
}
std::vector<std::string> Devices::motionCandidates(std::int64_t now) const {
    std::vector<std::string> result;
    if (!identifying(now)) return result;
    for (const auto& [rf, d] : devices_)
        if (d.connected && d.motion.candidate(now, policy_)) result.push_back(rf);
    return result; // multiple candidates remain ambiguous; never auto assign
}
namespace {
void resetEvidence(MotionEvidence& motion) {
    // Keep evaluation diagnostics (including rejected outliers) for this
    // Identify window while retiring all candidate evidence.
    motion.samples = motion.movements = 0;
    motion.positionDeltaMeters = motion.angularDeltaRadians = 0;
    motion.firstNs = motion.lastNs = 0;
}
double norm(const Quatf& q) { return std::hypot(std::hypot(double(q.x), q.y), std::hypot(double(q.z), q.w)); }
bool usableOrientation(const TrackerState& p) {
    const auto n = norm(p.orientationXyzw);
    return (p.trackingStatus == TrackingStatus::Ok || p.trackingStatus == TrackingStatus::RecentlyLost) &&
        std::isfinite(n) && n >= 0.5 && n <= 1.5;
}
bool usablePosition(const TrackerState& p) {
    const auto& v = p.positionMeters;
    return p.trackingStatus == TrackingStatus::Ok && std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
}
void Devices::observe(const BackendEvent& e) {
    if (e.transportGeneration != generation_ || e.observedMonotonicNs <= 0 || e.device.slot < 0 || e.device.slot >= 5) return;
    if (e.kind != BackendEvent::Kind::DevicePaired && e.kind != BackendEvent::Kind::DeviceUnpaired && e.kind != BackendEvent::Kind::Pose) return;
    if (e.kind == BackendEvent::Kind::Pose && (!e.pose || e.pose->device.toString() != e.device.toString() ||
        e.pose->device.slot != e.device.slot || e.pose->observedMonotonicNs != e.observedMonotonicNs)) return;
    const auto rf = e.device.toString();
    const auto existing = devices_.find(rf);
    if (existing != devices_.end() && e.observedMonotonicNs <= existing->second.lastSeenNs) return;
    if (existing == devices_.end() && devices_.size() >= 256) {
        warning_ = "Device history limit reached (256); restart setup to resume discovery";
        return;
    }
    if (e.kind != BackendEvent::Kind::DeviceUnpaired) {
        const auto previous = slots_.find(e.device.slot);
        if (previous != slots_.end() && previous->second != rf) {
            auto& old = devices_.at(previous->second);
            old.connected = false; old.identity.reset(); old.previous.reset(); old.motion = {};
            old.warning = "Stable hardware identity unavailable; persistent assignment blocked";
        }
        slots_[e.device.slot] = rf;
    }
    auto& d = devices_[rf];
    if (d.rfAddress.empty()) { d.rfAddress = rf; d.displayName = "VIVE Ultimate Tracker " + rf; }
    if (e.observedMonotonicNs <= d.lastSeenNs) return;
    d.lastSeenNs = e.observedMonotonicNs;
    if (e.kind == BackendEvent::Kind::DeviceUnpaired) {
        d.connected = false; d.identity.reset(); d.previous.reset(); d.motion = {};
        d.tracking = TrackingStatus::Unknown;
        d.warning = "Stable hardware identity unavailable; persistent assignment blocked";
        const auto slot = slots_.find(e.device.slot);
        if (slot != slots_.end() && slot->second == rf) slots_.erase(slot);
        return;
    }
    if (!d.connected) { d.lastPoseNs = 0; d.previous.reset(); d.motion = {}; }
    d.connected = true;
    if (e.kind == BackendEvent::Kind::DevicePaired) {
        d.identity.reset(); d.previous.reset(); d.motion = {}; d.lastPoseNs = 0;
        d.tracking = TrackingStatus::Unknown;
        d.warning = "Stable hardware identity unavailable; persistent assignment blocked";
    }
    if (e.kind != BackendEvent::Kind::Pose) return;
    const auto& p = *e.pose;
    d.lastPoseNs = e.observedMonotonicNs; d.tracking = p.trackingStatus;
    if (!identifying(e.observedMonotonicNs)) return;
    if (!usableOrientation(p)) { d.previous.reset(); resetEvidence(d.motion); return; }
    if (d.previous) {
        const auto gap = p.observedMonotonicNs - d.previous->observedMonotonicNs;
        d.motion.lastIntervalNs = gap; d.motion.maxGapNs = std::max(d.motion.maxGapNs, gap);
        const auto delta = static_cast<std::uint8_t>(p.packetIndex - d.previous->packetIndex);
        if (gap > policy_.freshnessNs) { d.previous.reset(); resetEvidence(d.motion); }
        else if (delta == 0 || delta >= 128) return;
    }
    auto& m = d.motion;
    if (m.samples == 0) m.firstNs = p.observedMonotonicNs;
    ++m.samples; m.lastNs = p.observedMonotonicNs;
    if (d.previous) {
        const auto& before = *d.previous;
        double distance = 0;
        if (usablePosition(p) && usablePosition(before)) {
            const auto& a = p.positionMeters; const auto& b = before.positionMeters;
            distance = std::hypot(std::hypot(double(a.x) - b.x, double(a.y) - b.y), double(a.z) - b.z);
        }
        const auto& a = p.orientationXyzw; const auto& b = before.orientationXyzw;
        const auto dot = (double(a.x)*b.x + double(a.y)*b.y + double(a.z)*b.z + double(a.w)*b.w) / (norm(a)*norm(b));
        const double angle = 2 * std::acos(std::clamp(std::abs(dot), 0.0, 1.0));
        m.maxPositionStepMeters = std::max(m.maxPositionStepMeters, distance);
        m.maxAngularStepRadians = std::max(m.maxAngularStepRadians, angle);
        // Ignore subthreshold noise. Reject discontinuities/localization jumps.
        if (distance > policy_.positionJumpMeters || angle > policy_.angularJumpRadians || before.trackingStatus != p.trackingStatus) {
            resetEvidence(m); d.previous = p; return;
        }
        if (distance >= policy_.positionNoiseMeters || angle >= policy_.angularNoiseRadians) {
            ++m.movements; m.positionDeltaMeters += distance; m.angularDeltaRadians += angle;
        }
    }
    d.previous = p;
}
}
