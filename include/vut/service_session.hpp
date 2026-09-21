#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace vut {

enum class ReconnectRevisionPolicy {
    Strict,
    PreserveOnTransportReconnect,
};

ReconnectRevisionPolicy parseReconnectRevisionPolicy(std::string_view value);
std::string_view reconnectRevisionPolicyName(ReconnectRevisionPolicy policy) noexcept;

struct CoordinateMapIdentity {
    std::string sourceId;
    std::map<std::string, std::string> spaces;
    bool operator==(const CoordinateMapIdentity&) const = default;
};

enum class SessionStartCause {
    InitialStart,
    TransportReconnect,
    ContinuityUncertain,
};

struct ServiceSession {
    std::string sessionId;
    std::int64_t epochNs = 0;
    std::uint64_t transportGeneration = 0;
    std::uint32_t revision = 0;
    bool revisionAdvanced = false;
};

// Separates transport/session lifetime from coordinate-map lifetime. The caller
// supplies the persistent journal advance operation so reuse never rewrites or
// rolls back the journal.
class SessionRevisionCoordinator {
public:
    explicit SessionRevisionCoordinator(
        ReconnectRevisionPolicy policy = ReconnectRevisionPolicy::Strict) noexcept;

    ServiceSession begin(
        const CoordinateMapIdentity& identity,
        SessionStartCause cause,
        std::uint64_t transportGeneration,
        const std::function<std::uint32_t()>& advanceRevision,
        const std::function<std::string()>& makeSessionId,
        const std::function<std::int64_t()>& readEpochNs);

private:
    ReconnectRevisionPolicy policy_;
    std::optional<CoordinateMapIdentity> identity_;
    std::optional<std::uint32_t> revision_;
    std::string lastSessionId_;
};

} // namespace vut
