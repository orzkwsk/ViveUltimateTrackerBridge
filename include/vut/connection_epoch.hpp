#pragma once

#include <cstdint>
#include "vut/types.hpp"
#include <optional>

namespace vut {

enum class ConnectionPhase {
    Associated, InfoCollecting, InfoComplete, PoseObserved, Disconnected, Invalidated
};
enum class InfoCollectionState { Collecting, Complete, Invalid };
enum class ConnectionEpochReason {
    None, DeviceUnpaired, BackendStopped, TransportLost, TransportGenerationChanged,
    Superseded, DuplicateToken, OutOfOrderOrMissingToken, MalformedToken,
    UnknownInfoToken, DestinationConflict, PoseBeforeInfo, EpochCounterExhausted,
    ProgressExhausted, AddressMoved, RegistryOwnershipAmbiguous,
    ObservationSessionRestarted, RuntimeShutdown
};

// Vendor-local transient receive evidence, never a stable identity or wire field.
// Token contents are not retained. Candidate addressing exists only while active.
struct ConnectionEpochSnapshot {
    std::uint64_t connectionEpochId = 0;
    std::uint64_t transportGeneration = 0;
    int slot = -1; // addressing metadata, never identity
    std::optional<DeviceId> deviceCandidate;
    ConnectionPhase phase = ConnectionPhase::Associated;
    InfoCollectionState infoState = InfoCollectionState::Collecting;
    std::uint8_t expectedIndex = 0; // bounded 0..9, also the progress count
    std::uint16_t seenMask = 0;
    bool active = false;
    bool infoComplete = false;
    bool initializationEligible = false; // eligibility evidence, no send trigger
    bool poseObserved = false; // arrival only, including Lost poses
    bool nadsPresent = false, nassPresent = false;
    bool nadsFresh = false, nassFresh = false; // active current epoch only
    ConnectionEpochReason reason = ConnectionEpochReason::None;
};

} // namespace vut
