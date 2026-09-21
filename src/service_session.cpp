#include "vut/service_session.hpp"

#include <stdexcept>

namespace vut {

ReconnectRevisionPolicy parseReconnectRevisionPolicy(std::string_view value) {
    if (value == "strict") return ReconnectRevisionPolicy::Strict;
    if (value == "preserve") return ReconnectRevisionPolicy::PreserveOnTransportReconnect;
    throw std::invalid_argument("reconnect_revision must be strict or preserve");
}

std::string_view reconnectRevisionPolicyName(ReconnectRevisionPolicy policy) noexcept {
    switch (policy) {
    case ReconnectRevisionPolicy::Strict: return "strict";
    case ReconnectRevisionPolicy::PreserveOnTransportReconnect: return "preserve";
    }
    return "strict";
}

SessionRevisionCoordinator::SessionRevisionCoordinator(ReconnectRevisionPolicy policy) noexcept
    : policy_(policy) {}

ServiceSession SessionRevisionCoordinator::begin(
    const CoordinateMapIdentity& identity,
    SessionStartCause cause,
    std::uint64_t transportGeneration,
    const std::function<std::uint32_t()>& advanceRevision,
    const std::function<std::string()>& makeSessionId,
    const std::function<std::int64_t()>& readEpochNs) {
    if (identity.sourceId.empty() || identity.spaces.empty())
        throw std::invalid_argument("coordinate map identity is incomplete");

    ServiceSession result;
    result.sessionId = makeSessionId();
    if (result.sessionId.empty() || result.sessionId == lastSessionId_)
        throw std::runtime_error("session ID generation did not produce a new identity");
    result.epochNs = readEpochNs();
    if (result.epochNs < 0) throw std::runtime_error("invalid monotonic epoch");
    result.transportGeneration = transportGeneration;

    const bool canPreserve =
        policy_ == ReconnectRevisionPolicy::PreserveOnTransportReconnect &&
        cause == SessionStartCause::TransportReconnect &&
        identity_ && revision_ && *identity_ == identity;
    if (canPreserve) {
        result.revision = *revision_;
    } else {
        result.revision = advanceRevision();
        result.revisionAdvanced = true;
    }

    identity_ = identity;
    revision_ = result.revision;
    lastSessionId_ = result.sessionId;
    return result;
}

} // namespace vut
