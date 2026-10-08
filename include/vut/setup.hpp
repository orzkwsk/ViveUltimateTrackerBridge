#pragma once

#include "vut/types.hpp"
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vut::setup {

enum class IdentityKind { HardwareSerial, StableUuid, PersistentIdentifier };
struct StableIdentity {
    IdentityKind kind;
    std::string identifier;
    [[nodiscard]] std::string key() const;
};
// Only call with identity evidence supplied by a verified device API. RF MAC,
// slot, packet index and session UUID are deliberately absent from this API.
StableIdentity stableIdentity(IdentityKind kind, std::string identifier);
StableIdentity parseStableIdentity(std::string_view key);

// Provisional synthetic-test defaults, HIL_REQUIRED. These are Identify tuning
// parameters, not device capability guarantees or verified physical thresholds.
struct IdentifyPolicy {
    std::size_t minimumSamples = 6, minimumMovements = 3;
    std::int64_t minimumSpanNs = 100000000, freshnessNs = 250000000, windowNs = 5000000000LL;
    double positionThresholdMeters = 0.12, angularThresholdRadians = 0.52;
    double positionNoiseMeters = 0.015, angularNoiseRadians = 0.05;
    double positionJumpMeters = 0.5, angularJumpRadians = 1.57;
    void validate() const;
};

struct MotionEvidence {
    std::size_t samples = 0, movements = 0;
    double positionDeltaMeters = 0, angularDeltaRadians = 0;
    double maxPositionStepMeters = 0, maxAngularStepRadians = 0;
    std::int64_t lastIntervalNs = 0, maxGapNs = 0;
    std::int64_t firstNs = 0, lastNs = 0;
    [[nodiscard]] bool candidate(std::int64_t now, const IdentifyPolicy&) const;
};
struct Device {
    std::string rfAddress, displayName;
    std::optional<StableIdentity> identity;
    bool connected = false;
    TrackingStatus tracking = TrackingStatus::Unknown;
    std::int64_t lastSeenNs = 0, lastPoseNs = 0;
    std::string warning = "Stable hardware identity unavailable; persistent assignment blocked";
    MotionEvidence motion;
    std::optional<TrackerState> previous;
};

class Devices {
public:
    explicit Devices(IdentifyPolicy policy = {});
    void beginSession(std::uint64_t generation);
    void endSession();
    void observe(const BackendEvent&);
    // Future identity-provider boundary; binding is intentionally session-only.
    // A reconnect/address change requires fresh hardware identity evidence.
    void observeIdentity(const std::string& rfAddress, const StableIdentity&);
    void beginIdentify(std::int64_t now);
    [[nodiscard]] std::vector<std::string> motionCandidates(std::int64_t now) const;
    [[nodiscard]] const std::map<std::string, Device>& entries() const { return devices_; }
    [[nodiscard]] bool identifying(std::int64_t now) const;
    [[nodiscard]] const std::string& warning() const { return warning_; }
    [[nodiscard]] const IdentifyPolicy& identifyPolicy() const { return policy_; }
private:
    IdentifyPolicy policy_;
    std::uint64_t generation_ = 0;
    std::optional<std::int64_t> identifyStart_;
    std::map<std::string, Device> devices_;
    std::map<int, std::string> slots_;
    std::string warning_;
};
}
