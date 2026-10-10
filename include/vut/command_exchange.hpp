#pragma once

#include <cstdint>

namespace vut {

// Host/API and framing evidence only. A matching response is not a vendor ACK.
enum class FeatureWriteState { NotAttempted, Failed, AcceptedByTransportApi, OutcomeUnknown };
enum class FeatureResponseState { NotObserved, Malformed, WrongCommandOnly, Matched };
enum class FeatureResponseObservation { None, Empty, Malformed, WrongCommand, Matched };
enum class FeatureFramingError { None, TooShort, InvalidDeclaredLength, DeclaredLengthExceedsBuffer };
enum class FeatureExchangeError {
    None, TransportUnavailable, PayloadBuildFailed, WriteRejected,
    WriteException, ResponseException, NoResponse, MalformedResponse, WrongCommandOnly
};
enum class SemanticEffect { Unverified, ConfirmedByIndependentEvidence, RejectedByIndependentEvidence };

struct FeatureExchangeEvidence {
    FeatureWriteState write = FeatureWriteState::NotAttempted;
    FeatureResponseState response = FeatureResponseState::NotObserved;
    SemanticEffect effect = SemanticEffect::Unverified;
    FeatureExchangeError error = FeatureExchangeError::None;
    FeatureResponseObservation lastObservation = FeatureResponseObservation::None;
    // Most recent malformed framing observed, retained even after a later match.
    FeatureFramingError framingError = FeatureFramingError::None;
    std::uint64_t transportGeneration = 0;
    std::uint8_t command = 0;
    unsigned responseAttempts = 0, emptyResponses = 0, malformedResponses = 0, wrongCommandResponses = 0;
    unsigned matchingMalformedResponses = 0;
    [[nodiscard]] bool matched() const noexcept {
        return write == FeatureWriteState::AcceptedByTransportApi && response == FeatureResponseState::Matched;
    }
};

enum class CleanupExchangeState { NotNeeded, Matched, Failed, Unknown };
struct PairingCleanupEvidence {
    CleanupExchangeState state = CleanupExchangeState::NotNeeded;
    SemanticEffect effect = SemanticEffect::Unverified;
    std::uint64_t enableGeneration = 0;
    FeatureExchangeEvidence disable;
};

inline const char* cleanupExchangeStateName(CleanupExchangeState state) noexcept {
    switch (state) {
    case CleanupExchangeState::NotNeeded: return "not_needed";
    case CleanupExchangeState::Matched: return "matched";
    case CleanupExchangeState::Failed: return "failed";
    case CleanupExchangeState::Unknown: return "unknown";
    }
    return "unknown";
}

} // namespace vut
