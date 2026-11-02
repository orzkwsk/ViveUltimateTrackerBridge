#pragma once
#include "vut/types.hpp"
#include "monaka/protocol/v2/codec.hpp"
#include <map>

namespace vut {
struct ObservationContext {
    std::string sourceId, sessionId;
    std::uint64_t generation = 0;
    std::uint32_t revision = 0;
    std::int64_t epochNs = 0;
    // Explicit per-MAC map identities; no axis-name or slot based fallback.
    std::map<std::string, std::string> spaces;
};

struct ObservationAdmissionDiagnostics {
    std::uint64_t acceptedPose = 0;
    std::uint64_t acceptedState = 0;
    std::uint64_t rejectGeneration = 0;
    std::uint64_t rejectBeforeEpoch = 0;
    std::uint64_t rejectUnknownDevice = 0;
    std::uint64_t rejectMissingPose = 0;
    std::uint64_t rejectPoseDeviceMismatch = 0;
    std::uint64_t rejectAbsent = 0;
    std::uint64_t rejectPoseTimestampMismatch = 0;
    std::uint64_t rejectPoseNonMonotonic = 0;
    std::uint64_t rejectDuplicateRf = 0;
    std::uint64_t rejectOldOrAmbiguousRf = 0;
    std::uint64_t rejectPoseGap = 0;
    std::uint64_t rejectPoseSequenceExhausted = 0;
    std::uint64_t rejectStateSequenceExhausted = 0;
    std::uint64_t rejectStateNonMonotonic = 0;
    std::uint64_t rejectRepairAfterAbsent = 0;
    // Local RF recovery diagnostics, independent of raw optical TrackingStatus.
    // Pending counts devices with quarantined candidates, never admitted poses.
    std::uint64_t resyncStarted = 0, resyncCompleted = 0, resyncReset = 0, resyncPending = 0;
    int lastAcceptedRf = -1;
    // Snapshot of the last RF duplicate/reorder/gap rejection, not the latest packet.
    int lastRejectedRf = -1;
    std::int64_t lastRejectedGapNs = -1;
    std::string lastUnknownDevice;
};

class ViveObservationAdapter {
public:
    enum class RestartReason {
        None,
        ConfiguredDevicePairedAfterAbsent,
        PoseSequenceExhausted,
        StateSequenceExhausted,
    };

    explicit ViveObservationAdapter(ObservationContext context);
    static monaka::protocol::v2::TrackerObservation mapPose(
        const TrackerState&, const ObservationContext&, std::int64_t sequence);
    std::optional<monaka::protocol::v2::Envelope> admit(const BackendEvent&);
    bool restartRequired() const { return restartReason_ != RestartReason::None; }
    RestartReason restartReason() const { return restartReason_; }
    // VIVE-owned classification: an exact configured device returning after an
    // observed absence is the narrow reconnect covered by the opt-in policy.
    // Exhaustion and future unclassified restart reasons remain fail-closed.
    bool coordinateContinuityUncertain() const {
        return restartReason_ != RestartReason::None &&
               restartReason_ != RestartReason::ConfiguredDevicePairedAfterAbsent;
    }
    std::uint64_t rejected() const { return rejected_; }
    const ObservationAdmissionDiagnostics& diagnostics() const { return diagnostics_; }
private:
    struct Stream {
        std::int64_t poseSequence = 0, stateSequence = 0, lastPoseNs = -1, lastStateNs = -1;
        struct Candidate {
            std::int64_t lastNs;
            std::uint8_t rf;
            unsigned packets;
        };
        std::optional<Candidate> candidate;
        std::uint8_t rf = 0;
        bool absent = false;
    };
    ObservationContext context_;
    std::map<std::string, Stream> streams_;
    std::uint64_t rejected_ = 0;
    ObservationAdmissionDiagnostics diagnostics_;
    RestartReason restartReason_ = RestartReason::None;
};
}
