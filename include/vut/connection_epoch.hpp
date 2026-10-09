#pragma once

#include <cstdint>

namespace vut {

enum class ConnectionPhase {
    Associated, InfoCollecting, InfoComplete, PoseObserved, Disconnected, Invalidated
};
enum class InfoCollectionState { Collecting, Complete, Invalid };
enum class ConnectionEpochReason {
    None, DeviceUnpaired, BackendStopped, TransportLost, TransportGenerationChanged,
    Superseded, DuplicateToken, OutOfOrderOrMissingToken, MalformedToken,
    UnknownInfoToken, DestinationConflict, PoseBeforeInfo, EpochCounterExhausted,
    ProgressExhausted
};

// Transient receive evidence only. No RF destination, NADS/NASS value, stable
// identity, initialized/Ready claim, or shared-protocol field is exposed.
struct ConnectionEpochSnapshot {
    std::uint64_t connectionEpochId = 0;
    std::uint64_t transportGeneration = 0;
    int slot = -1; // addressing metadata, never identity
    ConnectionPhase phase = ConnectionPhase::Associated;
    InfoCollectionState infoState = InfoCollectionState::Collecting;
    std::uint8_t expectedIndex = 0; // bounded 0..9, also the progress count
    std::uint16_t seenMask = 0;
    bool active = false;
    bool infoComplete = false;
    bool initializationEligible = false; // eligibility evidence, no send trigger
    bool poseObserved = false; // arrival only, including Lost poses
    ConnectionEpochReason reason = ConnectionEpochReason::None;
};

} // namespace vut
