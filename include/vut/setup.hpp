#pragma once

#include "vut/types.hpp"
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vut::setup {

// Local setup vocabulary, not a MonakaProtocol or upper consumer role enum.
const std::vector<std::string>& roles();
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

enum class ConflictAction { Cancel, Replace, Swap };
class Assignments {
public:
    void assign(const StableIdentity&, const std::string& role,
                ConflictAction = ConflictAction::Cancel);
    void unassign(const StableIdentity&);
    void swap(const StableIdentity&, const StableIdentity&);
    void clear();
    [[nodiscard]] std::string role(const StableIdentity&) const;
    [[nodiscard]] const std::map<std::string, std::string>& entries() const { return entries_; }
private:
    std::map<std::string, std::string> entries_;
};

struct MotionEvidence {
    std::size_t samples = 0, movements = 0;
    double positionDeltaMeters = 0, angularDeltaRadians = 0;
    std::int64_t firstNs = 0, lastNs = 0;
    [[nodiscard]] bool candidate(std::int64_t now) const;
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
private:
    std::uint64_t generation_ = 0;
    std::optional<std::int64_t> identifyStart_;
    std::map<std::string, Device> devices_;
    std::map<int, std::string> slots_;
    std::string warning_;
};
}
